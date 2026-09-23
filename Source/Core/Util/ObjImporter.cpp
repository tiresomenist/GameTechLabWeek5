#include "pch.h"
#include "Core/Util/ObjImporter.h"
#include "Core/Util/File.h"
#include "Core/Container/Map.h"
#include "Engine/Renderer/VertexSimple.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace
{
    //Parse 단계에서 에러가 터진 경우 호출
    [[noreturn]] void ParseError(const std::filesystem::path& Path, size_t LineNumber, FStringView Message)
    {
        // 오류 메시지의 경로도 UTF-8로 구성한다.
        const auto Utf8Path = Path.generic_u8string();
        const FString PathText(Utf8Path.begin(), Utf8Path.end());

        throw std::runtime_error(PathText + ":" + std::to_string(LineNumber) + ": " + FString(Message));
    }

    //공백 문자 여부
    bool IsSpace(char C)
    {
        return C == ' ' || C == '\t' || C == '\r' || C == '\v' || C == '\f';
    }


    //앞뒤 공백 문자 제거
    FStringView Trim(FStringView Text)
    {
        while (!Text.empty() && IsSpace(Text.front()))
        {
            Text.remove_prefix(1);
        }
        while (!Text.empty() && IsSpace(Text.back()))
        {
            Text.remove_suffix(1);
        }
        return Text;
    }

    // 원본 문자열을 참조하는 토큰을 반환한다.
    FStringView TakeToken(FStringView& Text)
    {
        while (!Text.empty() && IsSpace(Text.front()))
        {
            Text.remove_prefix(1);
        }
        if (Text.empty())
        {
            return {};
        }

        const char* Begin = Text.data();
        const char* Cursor = Begin;
        const char* End = Begin + Text.size();

        while (Cursor < End && !IsSpace(*Cursor))
        {
            ++Cursor;
        }

        const size_t Length = static_cast<size_t>(Cursor - Begin);
        Text.remove_prefix(Length);
        return FStringView(Begin, Length);
    }

    // 경로 토큰 하나를 복사 없이 읽고 Text를 전진시킨다. 큰따옴표로 묶인 경로의 형식도 검사한다.
    FStringView TakePathToken(FStringView& Text, const std::filesystem::path& Path, size_t LineNumber)
    {
        Text = Trim(Text);
        if (Text.empty() || Text.front() != '"')
        {
            return TakeToken(Text);
        }
        Text.remove_prefix(1);
        const size_t ClosingQuote = Text.find('"');
        if (ClosingQuote == FStringView::npos)
        {
            ParseError(Path, LineNumber, "Unclosed path quote");
        }

        const FStringView Result = Text.substr(0, ClosingQuote);
        Text.remove_prefix(ClosingQuote + 1);

        if (Result.empty() || (!Text.empty() && !IsSpace(Text.front())))
        {
            ParseError(Path, LineNumber, "Invalid quoted path");
        }

        return Result;
    }

    //obj파일의 각 라인을 읽는 함수
    template<typename Callback>
    void ForEachLine(FStringView Text, const std::filesystem::path& Path, Callback&& ProcessLine)
    {
        // UTF-8 BOM 건너뛰기.
        if (Text.size() >= 3
            && static_cast<unsigned char>(Text[0]) == 0xEF
            && static_cast<unsigned char>(Text[1]) == 0xBB
            && static_cast<unsigned char>(Text[2]) == 0xBF)
        {
            Text.remove_prefix(3);
        }

        if (Text.empty())
        {
            return;
        }

        const char* Cursor = Text.data();
        const char* End = Cursor + Text.size();
        size_t LineNumber = 0;

        while (Cursor < End)
        {
            ++LineNumber;
            const char* Begin = Cursor;

            //커서를 문장 끝까지 이동시킴
            while (Cursor < End && *Cursor != '\n')
            {
                ++Cursor;
            }

            //한줄 읽음
            FStringView Line(Begin, static_cast<size_t>(Cursor - Begin));

            //다음줄이 있으면 커서를 다음줄로 이동
            if (Cursor < End)
            {
                ++Cursor;
            }

            //주석 여부 확인
            const size_t Comment = Line.find('#');

            if (Comment != FStringView::npos)
            {
                Line = Line.substr(0, Comment);
            }

            // 앞뒤 공백 제거
            Line = Trim(Line);

            if (Line.empty())
            {
                continue;
            }

            // 줄이음은 지원하지 않음
            if (Line.back() == '\\')
            {
                ParseError(Path, LineNumber, "Line continuation is not supported");
            }
            // 예외처리 후, 등록한 ProcessLine 함수 호출
            ProcessLine(Line, LineNumber);
        }
    }

    // 들어온 값을 숫자 형식으로 전환
    template<typename T>
    T ReadNumber(FStringView Token, const std::filesystem::path& Path, size_t LineNumber)
    {
        // 비어있는 토큰 처리
        if (Token.empty())
        {
            ParseError(Path, LineNumber, "Missing number");
        }

        // from_chars가 받지 않는 선행 '+' 처리
        if (Token.front() == '+')
        {
            Token.remove_prefix(1);

            if (Token.empty() || Token.front() == '+' || Token.front() == '-')
            {
                ParseError(Path, LineNumber, "Invalid number");
            }
        }

        T Value{};
        const char* Begin = Token.data();
        const char* End = Begin + Token.size();

        //from_chars를 통해 토큰을 숫자로 전환
        const auto Result = std::from_chars(Begin, End, Value);

        //변환 실패
        if (Result.ec != std::errc{} || Result.ptr != End)
        {
            ParseError(Path, LineNumber, "Invalid number");
        }


        if constexpr (std::is_floating_point_v<T>)
        {
            //무한대 처리
            if (!std::isfinite(Value))
            {
                ParseError(Path, LineNumber, "Non-finite number");
            }
        }

        return Value;
    }

    // float 읽어오기
    float TakeFloat(FStringView& Text, const std::filesystem::path& Path, size_t LineNumber)
    {
        return ReadNumber<float>(TakeToken(Text), Path, LineNumber);
    }

    // 벡터 읽어오기
    FVector TakeVector3(FStringView& Text, const std::filesystem::path& Path, size_t LineNumber)
    {
        FVector Result;
        Result.X = TakeFloat(Text, Path, LineNumber);
        Result.Y = TakeFloat(Text, Path, LineNumber);
        Result.Z = TakeFloat(Text, Path, LineNumber);
        return Result;
    }

    // 필요한 값을 읽은 뒤 남은 내용이 공백뿐인지 검사하는 함수
    void RequireEnd(FStringView Text, const std::filesystem::path& Path, size_t LineNumber)
    {
        if (!Trim(Text).empty())
        {
            ParseError(Path, LineNumber, "Unexpected extra values");
        }
    }

    // obj의 1기반 인덱스 를 c++의 0기반 인덱스로 전환
    int32 ResolveIndex(FStringView Token, int32 Count, const std::filesystem::path& Path, size_t LineNumber)
    {
        const int32 ObjIndex = ReadNumber<int32>(Token, Path, LineNumber);

        const int64 Index = ObjIndex > 0 ? static_cast<int64>(ObjIndex) - 1 : static_cast<int64>(Count) + ObjIndex;

        if (ObjIndex == 0 || Index < 0 || Index >= Count)
        {
            ParseError(Path, LineNumber, "OBJ index out of range");
        }

        return static_cast<int32>(Index);
    }

    //f 인덱스 집합 부분을 읽어 위치, uv, 법선 인덱스로 분리
    FObjVertexIndex ReadCorner(FStringView Token, const FObjInfo& Info, const std::filesystem::path& Path,
        size_t LineNumber)
    {
        FObjVertexIndex Result;
        const size_t Slash1 = Token.find('/');

        Result.PositionIndex = ResolveIndex(Token.substr(0, Slash1), Info.Positions.Num(), Path, LineNumber);

        if (Slash1 == FStringView::npos)
        {
            return Result; // v
        }

        const size_t Slash2 = Token.find('/', Slash1 + 1);

        if (Slash2 == FStringView::npos)
        {
            Result.UVIndex = ResolveIndex(Token.substr(Slash1 + 1), Info.TexCoords.Num(), Path, LineNumber);

            return Result; // v/vt
        }

        if (Token.find('/', Slash2 + 1) != FStringView::npos)
        {
            ParseError(Path, LineNumber, "Too many index separators");
        }

        const FStringView UVToken = Token.substr(Slash1 + 1, Slash2 - Slash1 - 1);

        if (!UVToken.empty())
        {
            Result.UVIndex = ResolveIndex(UVToken, Info.TexCoords.Num(), Path, LineNumber);
        }

        Result.NormalIndex = ResolveIndex(Token.substr(Slash2 + 1), Info.Normals.Num(), Path, LineNumber);

        return Result; // v//vn 또는 v/vt/vn
    }

    // 머티리얼의 이름으로 머티리얼을 맵에 등록하거나 키값을 가져와서, 검색용 인덱스를 뱉는 함수
    int32 GetOrAddMaterial(FObjInfo& Info, TMap<FString, int32>& MaterialLookup, FStringView Name)
    {
        FString Key(Name);

        if (const int32* FoundIndex = MaterialLookup.Find(Key))
        {
            return *FoundIndex;
        }

        const int32 NewIndex = Info.Materials.Num();

        FObjMaterialInfo Material;
        Material.Name = std::move(Key);

        Info.Materials.Add(std::move(Material));

        MaterialLookup.Add(Info.Materials[NewIndex].Name, NewIndex);

        return NewIndex;
    }

    //파일 경로를 연결하는 함수
    std::filesystem::path ResolvePath(const std::filesystem::path& Parent, FStringView Name)
    {
        const std::filesystem::path Relative = std::filesystem::u8path(Name.begin(), Name.end());

        return (Parent / Relative).lexically_normal();
    }

    // 실제로 읽은 원본 파일의 절대 경로를 중복 없이 기록한다.
    void AddSourceFile(FObjInfo& Info, const std::filesystem::path& Path)
    {
        // 상대 경로와 불필요한 "."·".."를 정리해 변경 검사 기준을 통일한다.
        const std::filesystem::path AbsolutePath =
            std::filesystem::absolute(Path).lexically_normal();

        // 같은 경로가 반복 선언돼도 원본 목록에는 한 번만 추가한다.
        for (const std::filesystem::path& ExistingPath : Info.SourceFiles)
        {
            if (ExistingPath == AbsolutePath) return;
        }
        Info.SourceFiles.Add(AbsolutePath);
    }
    
    // 다음 토큰 전체가 실수 형식일 때만 선택 성분으로 읽는다.
    bool TryTakeTextureFloat(FStringView& Text, const std::filesystem::path& Path,
        size_t LineNumber, float& OutValue)
    {
        // 파일명이나 다음 옵션이면 원본 읽기 위치를 유지한다.
        FStringView Remaining = Text;
        const FStringView Token = TakeToken(Remaining);
        FStringView Probe = Token;
        if (!Probe.empty() && Probe.front() == '+') Probe.remove_prefix(1);
        if (Probe.empty()) return false;

        // 숫자 일부로 시작하는 파일명은 전체 숫자로 취급하지 않는다.
        float Ignored = 0.0f;
        const char* Begin = Probe.data();
        const char* End = Begin + Probe.size();
        const auto Parsed = std::from_chars(Begin, End, Ignored);
        if (Parsed.ptr != End ||
            (Parsed.ec != std::errc{} && Parsed.ec != std::errc::result_out_of_range))
            return false;

        // 범위 초과와 NaN·무한대 검사는 기존 숫자 파서에 맡긴다.
        OutValue = ReadNumber<float>(Token, Path, LineNumber);
        Text = Remaining;
        return true;
    }

    // 필수 u와 선택 v·w를 읽고 생략된 성분은 지정된 기본값으로 유지한다.
    FVector TakeTextureVector(FStringView& Text, const std::filesystem::path& Path,
        size_t LineNumber, FVector Defaults)
    {
        // 첫 성분은 반드시 필요하며, 이후 성분은 숫자가 있을 때만 소비한다.
        Defaults.X = TakeFloat(Text, Path, LineNumber);
        if (TryTakeTextureFloat(Text, Path, LineNumber, Defaults.Y))
            TryTakeTextureFloat(Text, Path, LineNumber, Defaults.Z);
        return Defaults;
    }
    
    // 텍스처 선언의 옵션과 파일 경로를 읽어 지정된 맵에 저장한다.
    void ReadTextureMap(FStringView Line, const std::filesystem::path& Path,
        size_t LineNumber, std::filesystem::path& OutPath,
        FStaticMeshTextureOptions& OutOptions, bool bIsBumpMap)
    {
        // 같은 맵을 다시 선언하면 이전 옵션을 상속하지 않고 기본값부터 읽는다.
        FStaticMeshTextureOptions Options{};
        Line = Trim(Line);

        // 파일명 앞에 있는 옵션을 순서대로 읽는다.
        while (!Line.empty() && Line.front() == '-')
        {
            const FStringView Option = TakeToken(Line);
            if (Option == "-clamp")
            {
                const FStringView Value = TakeToken(Line);
                if (Value == "on") Options.bClamp = true;
                else if (Value == "off") Options.bClamp = false;
                else ParseError(Path, LineNumber, "Expected on or off after -clamp");
            }
            else if (Option == "-o")
            {
                // 이동량의 생략된 성분은 0으로 채운다.
                Options.Offset = TakeTextureVector(Line, Path, LineNumber, FVector{});
            }
            else if (Option == "-s")
            {
                // 배율의 생략된 성분은 1로 채운다.
                Options.Scale = TakeTextureVector(Line, Path, LineNumber, FVector{ 1.0f, 1.0f, 1.0f });
            }
            else if (Option == "-bm")
            {
                // 범프 전용 옵션이며 숫자 형식과 유한값 검사는 기존 함수를 재사용한다.
                if (!bIsBumpMap)
                    ParseError(Path, LineNumber, "-bm is only supported for bump maps.");
                Options.BumpMultiplier = TakeFloat(Line, Path, LineNumber);
            }
            else
            {
                ParseError(Path, LineNumber, "Unsupported texture map option: " + FString(Option));
            }
            Line = Trim(Line);
        }

        // 옵션 뒤의 나머지를 경로로 읽으며 기존 공백·따옴표 처리를 유지한다.
        FStringView TextureName = Trim(Line);
        if (TextureName.empty())
            ParseError(Path, LineNumber, "Missing texture map path");

        if (TextureName.front() == '"')
        {
            FStringView Remaining = TextureName;
            TextureName = TakePathToken(Remaining, Path, LineNumber);
            RequireEnd(Remaining, Path, LineNumber);
        }

        // 선언 전체를 읽은 뒤 경로와 옵션을 함께 갱신한다.
        OutPath = ResolvePath(Path.parent_path(), TextureName);
        OutOptions = Options;
    }

    // MTL의 머티리얼 수치와 용도별 텍스처 경로를 읽어 CPU 데이터에 보관한다.
    void ReadMtl(const std::filesystem::path& Path, FObjInfo& Info, TMap<FString, int32>& MaterialLookup)
    {
        const FString Text = File::ReadTextFromPath(Path);
        AddSourceFile(Info, Path);
        int32 CurrentMaterial = -1;

        ForEachLine(Text, Path, [&](FStringView Line, size_t LineNumber)
            {
                const FStringView Prefix = TakeToken(Line);

                if (Prefix == "newmtl")
                {
                    const FStringView Name = Trim(Line);

                    if (Name.empty())
                    {
                        ParseError(Path, LineNumber, "Missing material name");
                    }

                    CurrentMaterial = GetOrAddMaterial(Info, MaterialLookup, Name);

                    FObjMaterialInfo& Material = Info.Materials[CurrentMaterial];

                    // 같은 이름을 재정의하면 나중 정의 사용.
                    Material = FObjMaterialInfo{};
                    Material.Name = FString(Name);
                    Material.bDefined = true;
                    return;
                }
                const bool bColor = Prefix == "Ka" || Prefix == "Kd"
                    || Prefix == "Ks" || Prefix == "Ke";
                const bool bScalar = Prefix == "Ns" || Prefix == "Ni"
                    || Prefix == "d" || Prefix == "Tr";

                // 범프 맵의 여러 표기를 하나의 종류로 취급한다.
                const bool bBumpTexture = Prefix == "bump"
                    || Prefix == "map_bump" || Prefix == "map_Bump";

                // 지원하는 맵은 모두 기존 텍스처 경로 처리 분기로 전달한다.
                const bool bTexture = Prefix == "map_Kd" || Prefix == "map_d"
                    || Prefix == "map_Ka" || Prefix == "map_Ks"
                    || Prefix == "map_Ke" || Prefix == "map_Ns"
                    || bBumpTexture || Prefix == "norm" || Prefix == "disp";

                // 아직 지원하지 않는 지시문은 기존처럼 건너뛴다.
                if (!bColor && !bScalar && Prefix != "illum" && !bTexture)
                    return;
                if (CurrentMaterial < 0)
                    ParseError(Path, LineNumber, "Material property appears before newmtl");

                FObjMaterialInfo& Material = Info.Materials[CurrentMaterial];

                if (bColor)
                {
                    // 지시문에 대응하는 필드만 선택하고 RGB 읽기 로직은 공유한다.
                    FVector* Color = &Material.DiffuseColor;
                    if (Prefix == "Ka") Color = &Material.AmbientColor;
                    else if (Prefix == "Ks") Color = &Material.SpecularColor;
                    else if (Prefix == "Ke") Color = &Material.EmissiveColor;

                    *Color = TakeVector3(Line, Path, LineNumber);
                    RequireEnd(Line, Path, LineNumber);
                }
                else if (bScalar)
                {
                    // Tr은 투명도이므로 기존 정책대로 불투명도 1-Tr로 변환한다.
                    const float Value = TakeFloat(Line, Path, LineNumber);
                    RequireEnd(Line, Path, LineNumber);

                    if (Prefix == "Ns") Material.SpecularExponent = Value;
                    else if (Prefix == "Ni") Material.RefractionIndex = Value;
                    else Material.Opacity = Prefix == "Tr" ? 1.0f - Value : Value;
                }
                else if (Prefix == "illum")
                {
                    // 조명 모델은 실수가 아닌 정수 번호로 보관한다.
                    Material.IlluminationModel =
                        ReadNumber<int32>(TakeToken(Line), Path, LineNumber);
                    RequireEnd(Line, Path, LineNumber);
                }
                else
                {
                    // 선택한 맵의 경로와 옵션에 공통 파싱 함수를 적용한다.
                    auto ReadMap = [&](std::filesystem::path& TexturePath,
                        FStaticMeshTextureOptions& TextureOptions)
                        {
                            ReadTextureMap(Line, Path, LineNumber, TexturePath, TextureOptions, bBumpTexture);
                        };
                    // 지시문에 대응하는 경로와 옵션을 반드시 같은 쌍으로 전달한다.
                    if (Prefix == "map_Kd")
                        ReadMap(Material.DiffuseTexturePath, Material.DiffuseTextureOptions);
                    else if (Prefix == "map_d")
                        ReadMap(Material.OpacityTexturePath, Material.OpacityTextureOptions);
                    else if (Prefix == "map_Ka")
                        ReadMap(Material.AmbientTexturePath, Material.AmbientTextureOptions);
                    else if (Prefix == "map_Ks")
                        ReadMap(Material.SpecularTexturePath, Material.SpecularTextureOptions);
                    else if (Prefix == "map_Ke")
                        ReadMap(Material.EmissiveTexturePath, Material.EmissiveTextureOptions);
                    else if (Prefix == "map_Ns")
                        ReadMap(Material.SpecularExponentTexturePath, Material.SpecularExponentTextureOptions);
                    else if (bBumpTexture)
                        ReadMap(Material.BumpTexturePath, Material.BumpTextureOptions);
                    else if (Prefix == "norm")
                        ReadMap(Material.NormalTexturePath, Material.NormalTextureOptions);
                    else if (Prefix == "disp")
                        ReadMap(Material.DisplacementTexturePath, Material.DisplacementTextureOptions);
                }

                // 바이너리 복원과 동일한 수치 검사로 잘못된 머티리얼을 Import에서 거부한다.
                if (!Material.HasValidNumericValues())
                    ParseError(Path, LineNumber, "Invalid material numeric values");
            });
    }
    // 투영 전에는 정규화된 3D 좌표, 투영 후에는 XY 평면 좌표를 보관한다.
    struct FEarPoint
    {
        double X = 0.0, Y = 0.0, Z = 0.0;
    };

    // 2D 삼각형의 방향과 두 배 부호 면적을 계산한다.
    double EarCross(const FEarPoint& A, const FEarPoint& B, const FEarPoint& C)
    {
        return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
    }

    // 두 선분이 교차하거나 접촉하는지 허용 오차를 포함해 검사한다.
    bool EarSegmentsIntersect(const FEarPoint& A, const FEarPoint& B,
        const FEarPoint& C, const FEarPoint& D, double Epsilon)
    {
        // 범위가 분리된 선분은 교차할 수 없다.
        if (std::max(A.X, B.X) < std::min(C.X, D.X) - Epsilon
            || std::max(C.X, D.X) < std::min(A.X, B.X) - Epsilon
            || std::max(A.Y, B.Y) < std::min(C.Y, D.Y) - Epsilon
            || std::max(C.Y, D.Y) < std::min(A.Y, B.Y) - Epsilon)
            return false;

        const double ABC = EarCross(A, B, C), ABD = EarCross(A, B, D);
        const double CDA = EarCross(C, D, A), CDB = EarCross(C, D, B);
        return !((ABC > Epsilon && ABD > Epsilon) || (ABC < -Epsilon && ABD < -Epsilon)
            || (CDA > Epsilon && CDB > Epsilon) || (CDA < -Epsilon && CDB < -Epsilon));
    }

    // 단순 평면 다각형을 Ear Clipping으로 나누고 원본 코너와 면 속성을 보존한다.
    TArray<FObjTriangle> TriangulateFace(const FObjInfo& Info,
        const TArray<FObjVertexIndex>& Corners, const FObjTriangle& FaceInfo,
        const std::filesystem::path& Path, size_t LineNumber)
    {
        const int32 Count = Corners.Num();
        if (Count < 3) ParseError(Path, LineNumber, "A face requires at least three corners");

        TArray<FObjTriangle> Triangles;
        Triangles.Reserve(static_cast<size_t>(Count - 2));

        // 출력은 원본 코너를 복사하므로 UV·법선 및 면 속성이 유지된다.
        auto AddTriangle = [&](int32 A, int32 B, int32 C)
            {
                FObjTriangle Triangle = FaceInfo;
                Triangle.Corners[0] = Corners[A];
                Triangle.Corners[1] = Corners[B];
                Triangle.Corners[2] = Corners[C];
                Triangles.Add(Triangle);
            };

        // 이미 삼각형인 면은 기존 퇴화 필터와 출력 순서를 유지한다.
        if (Count == 3)
        {
            const FVector& A = Info.Positions[Corners[0].PositionIndex];
            const FVector& B = Info.Positions[Corners[1].PositionIndex];
            const FVector& C = Info.Positions[Corners[2].PositionIndex];
            if ((B - A).Cross(C - A).LengthSquared() > EPSILON * EPSILON)
                AddTriangle(0, 1, 2);
            return Triangles;
        }

        // 큰 절대 좌표와 모델 크기의 영향을 줄이기 위해 원점 이동 후 정규화한다.
        const FVector& Origin = Info.Positions[Corners[0].PositionIndex];
        TArray<FEarPoint> Points;
        Points.Reserve(static_cast<size_t>(Count));
        double Scale = 0.0;
        for (const FObjVertexIndex& Corner : Corners)
        {
            const FVector& P = Info.Positions[Corner.PositionIndex];
            FEarPoint Q{ double(P.X) - Origin.X, double(P.Y) - Origin.Y, double(P.Z) - Origin.Z };
            Scale = std::max(Scale, std::max({ std::abs(Q.X), std::abs(Q.Y), std::abs(Q.Z) }));
            Points.Add(Q);
        }
        if (Scale == 0.0) return {};
        for (FEarPoint& P : Points) { P.X /= Scale; P.Y /= Scale; P.Z /= Scale; }

        // 경계 전체의 면적 벡터로 투영 방향과 기준 평면을 구한다.
        constexpr double Epsilon = 1e-12;
        constexpr double PlaneTolerance = 1e-5;
        FEarPoint Normal;
        for (int32 I = 0; I < Count; ++I)
        {
            const FEarPoint& A = Points[I];
            const FEarPoint& B = Points[(I + 1) % Count];
            Normal.X += A.Y * B.Z - A.Z * B.Y;
            Normal.Y += A.Z * B.X - A.X * B.Z;
            Normal.Z += A.X * B.Y - A.Y * B.X;
        }
        const double Length = std::sqrt(Normal.X * Normal.X + Normal.Y * Normal.Y + Normal.Z * Normal.Z);
        if (Length <= Epsilon)
        {
            FEarPoint Axis;
            double AxisLengthSquared = 0.0;
            for (const FEarPoint& P : Points)
            {
                const double CandidateLength = P.X * P.X + P.Y * P.Y + P.Z * P.Z;
                if (CandidateLength > AxisLengthSquared)
                {
                    Axis = P;
                    AxisLengthSquared = CandidateLength;
                }
            }

            // 첫 점에서 가장 먼 점으로 향하는 직선 밖의 점이 있으면 퇴화로 단정하지 않는다.
            for (const FEarPoint& P : Points)
            {
                const double CX = Axis.Y * P.Z - Axis.Z * P.Y;
                const double CY = Axis.Z * P.X - Axis.X * P.Z;
                const double CZ = Axis.X * P.Y - Axis.Y * P.X;
                if (CX * CX + CY * CY + CZ * CZ > Epsilon * Epsilon * AxisLengthSquared)
                    ParseError(Path, LineNumber, "Polygon has no stable plane");
            }
            return {};
        }
        Normal.X /= Length; Normal.Y /= Length; Normal.Z /= Length;

        const double NX = std::abs(Normal.X), NY = std::abs(Normal.Y), NZ = std::abs(Normal.Z);
        const int32 DropAxis = NX >= NY && NX >= NZ ? 0 : (NY >= NZ ? 1 : 2);
        for (FEarPoint& P : Points)
        {
            if (std::abs(P.X * Normal.X + P.Y * Normal.Y + P.Z * Normal.Z) > PlaneTolerance)
                ParseError(Path, LineNumber, "Non-planar polygon is not supported");

            if (DropAxis == 0) P = { P.Y, P.Z, 0.0 };
            else if (DropAxis == 1) P = { P.X, P.Z, 0.0 };
            else P.Z = 0.0;
        }

        // 중복·겹친 간선이 있는 면은 제외하고, 정상 경계의 진행 방향을 구한다.
        double Area = 0.0;
        for (int32 I = 0; I < Count; ++I)
        {
            const FEarPoint& A = Points[(I + Count - 1) % Count];
            const FEarPoint& B = Points[I];
            const FEarPoint& C = Points[(I + 1) % Count];
            const double DX = C.X - B.X, DY = C.Y - B.Y;

            // 길이 없는 간선이나 이전 간선을 되짚는 경계는 면 전체를 제외한다.
            if (DX * DX + DY * DY <= Epsilon * Epsilon
                || (std::abs(EarCross(A, B, C)) <= Epsilon
                    && (B.X - A.X) * DX + (B.Y - A.Y) * DY < 0.0))
                return {};

            Area += B.X * C.Y - B.Y * C.X;
        }
        if (std::abs(Area) <= Epsilon) return {};
        const double Winding = Area > 0.0 ? 1.0 : -1.0;

        // 이웃하지 않는 경계의 교차·접촉은 복구하지 않고 오류로 처리한다.
        for (int32 I = 0; I < Count; ++I)
        {
            const int32 NextI = (I + 1) % Count;
            for (int32 J = I + 1; J < Count; ++J)
            {
                const int32 NextJ = (J + 1) % Count;
                if (NextI == J || NextJ == I) continue;
                if (EarSegmentsIntersect(Points[I], Points[NextI], Points[J], Points[NextJ], Epsilon))
                    ParseError(Path, LineNumber, "Polygon boundary intersects or touches itself");
            }
        }

        // 원본 코너 배열은 유지하고 아직 제거하지 않은 코너 번호만 관리한다.
        TArray<int32> Remaining;
        Remaining.Reserve(static_cast<size_t>(Count));
        for (int32 I = 0; I < Count; ++I) Remaining.Add(I);
        while (Remaining.Num() > 3)
        {
            bool bClipped = false;
            const int32 Num = Remaining.Num();
            for (int32 I = 0; I < Num; ++I)
            {
                const int32 A = Remaining[(I + Num - 1) % Num];
                const int32 B = Remaining[I];
                const int32 C = Remaining[(I + 1) % Num];
                if (Winding * EarCross(Points[A], Points[B], Points[C]) <= Epsilon) continue;

                // 삼각형 내부나 경계에 다른 꼭짓점이 있으면 귀로 선택하지 않는다.
                bool bBlocked = false;
                for (int32 P : Remaining)
                {
                    if (P == A || P == B || P == C) continue;
                    if (Winding * EarCross(Points[A], Points[B], Points[P]) >= -Epsilon
                        && Winding * EarCross(Points[B], Points[C], Points[P]) >= -Epsilon
                        && Winding * EarCross(Points[C], Points[A], Points[P]) >= -Epsilon)
                    {
                        bBlocked = true;
                        break;
                    }
                }
                if (bBlocked) continue;

                AddTriangle(A, B, C);
                Remaining.RemoveAt(static_cast<size_t>(I));
                bClipped = true;
                break;
            }
            if (!bClipped) ParseError(Path, LineNumber, "Cannot triangulate polygon");
        }

        // 마지막 삼각형이 퇴화했다면 해당 면에서 만든 중간 결과까지 모두 제외한다.
        const double FinalArea = Winding * EarCross(
            Points[Remaining[0]], Points[Remaining[1]], Points[Remaining[2]]);
        if (FinalArea < -Epsilon)
            ParseError(Path, LineNumber, "Invalid final triangle winding");
        if (FinalArea <= Epsilon) return {};

        AddTriangle(Remaining[0], Remaining[1], Remaining[2]);
        return Triangles;
    }

}
// OBJ와 참조 MTL을 읽고 면을 EarClipping로 삼각분할하여 FObjInfo를 반환하는 함수
FObjInfo FObjImporter::Import(const std::filesystem::path& Path)
{
    //리턴값
    FObjInfo Info;
    Info.PathFileName = Path.lexically_normal();
    
    //머티리얼 배열 인덱스
    TMap<FString, int32> MaterialLookup;

    //파일 전체 txt
    const FString Text = File::ReadTextFromPath(Path);
    AddSourceFile(Info, Path);

    // 현재 객체/머티리얼/스무딩 그룹.
    int32 CurrentObject = -1;
    int32 CurrentMaterial = -1;
    uint32 CurrentSmoothingGroup = 0;

    // 각 라인에 대해
    ForEachLine(Text, Path,
        [&](FStringView Line, size_t LineNumber)
        {
            const FStringView Prefix = TakeToken(Line);

            //정점 정보
            if (Prefix == "v")
            {
                float Values[6]{};
                int32 Count = 0;
                while (true)
                {
                    const FStringView Token = TakeToken(Line);
                    if (Token.empty()){ break; }
                    if (Count >= 6)
                    {
                        ParseError(Path, LineNumber,"Too many vertex values");
                    }
                    Values[Count] = ReadNumber<float>(Token, Path, LineNumber);
                    ++Count;
                }

                //정점 개수가 3,4,6이 아닐때
                if (Count != 3 && Count != 4 && Count != 6)
                {
                    ParseError(Path, LineNumber,"Expected xyz, xyzw, or xyzrgb");
                }

                const FVector Position{ Values[0], Values[1], Values[2] };
                FVector4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
                // W값 존재
                if (Count == 4)
                {
                    // 기존 w 처리 정책 유지.
                    if (Values[3] != 1.0f)
                    {
                        ParseError(Path, LineNumber, "Non-unit vertex weight is not supported");
                    }
                }
                //RGB값 존재
                else if (Count == 6)
                {
                    // RGB가 0~1임을 가정
                    for (int32 Index = 3; Index < 6; ++Index)
                    {
                        if (Values[Index] < 0.0f || Values[Index] > 1.0f)
                        {
                            ParseError(Path, LineNumber, "Vertex RGB must be between 0 and 1");
                        }
                    }

                    Color = FVector4{Values[3], Values[4], Values[5], 1.0f};
                }
                Info.Positions.Add(Position);
                Info.VertexColors.Add(Color);
            }

            //UV값
            else if (Prefix == "vt")
            {
                FVector UVW{};
                UVW.X = TakeFloat(Line, Path, LineNumber);
                const FStringView VToken = TakeToken(Line);
                if (!VToken.empty())
                {
                    UVW.Y = ReadNumber<float>(VToken, Path, LineNumber);
                }
                const FStringView WToken = TakeToken(Line);
                if (!WToken.empty())
                {
                    UVW.Z = ReadNumber<float>(WToken, Path, LineNumber);
                }
                RequireEnd(Line, Path, LineNumber);
                Info.TexCoords.Add(UVW);
            }
            //법선벡터값
            else if (Prefix == "vn")
            {
                const FVector Normal = TakeVector3(Line, Path, LineNumber);
                RequireEnd(Line, Path, LineNumber);
                Info.Normals.Add(Normal);
            }
            //각 꼭짓점의 참조를 읽는 부분
            else if (Prefix == "f")
            {
                // 한 면의 꼭짓점을 원본 순서대로 모으고 기존 인덱스 검증을 재사용한다.
                TArray<FObjVertexIndex> Corners;
                while (true)
                {
                    const FStringView Token = TakeToken(Line);
                    if (Token.empty()) break;
                    Corners.Add(ReadCorner(Token, Info, Path, LineNumber));
                }
                if (Corners.Num() < 3)
                    ParseError(Path, LineNumber, "A face requires at least three corners");

                // o 선언 전의 면은 기존처럼 이름 없는 기본 객체에 소속시킨다.
                if (CurrentObject < 0)
                {
                    FObjObjectInfo Object;
                    CurrentObject = Info.Objects.Num();
                    Info.Objects.Add(std::move(Object));
                }

                // 면의 공통 속성을 모든 출력 삼각형에 전달한다.
                FObjTriangle FaceInfo;
                FaceInfo.ObjectIndex = CurrentObject;
                FaceInfo.MaterialIndex = CurrentMaterial;
                FaceInfo.SmoothingGroup = CurrentSmoothingGroup;
                const TArray<FObjTriangle> FaceTriangles = TriangulateFace(Info, Corners, FaceInfo, Path, LineNumber);
                for (const FObjTriangle& Triangle : FaceTriangles)
                    Info.Triangles.Add(Triangle);
            }
            //객체 구분을 읽는 부분
            else if (Prefix == "o")
            {
                FObjObjectInfo Object;

                // Text가 소멸한 뒤에도 필요하므로 이름은 소유 복사.
                Object.Name = FString(Trim(Line));

                CurrentObject = Info.Objects.Num();
                Info.Objects.Add(std::move(Object));
            }

            //머티리얼을 읽는 부분
            else if (Prefix == "mtllib")
            {
                bool bHasLibrary = false;
                while (true)
                {
                    const FStringView LibraryName = TakePathToken(Line, Path, LineNumber);
                    if (LibraryName.empty()) { break; }

                    bHasLibrary = true;

                    // MTL 경로는 OBJ 파일 위치 기준.
                    const std::filesystem::path MtlPath = ResolvePath(Path.parent_path(), LibraryName);

                    ReadMtl(MtlPath, Info, MaterialLookup);
                }

                if (!bHasLibrary)
                {
                    ParseError(Path, LineNumber, "Missing MTL file name");
                }
            }
            else if (Prefix == "usemtl")
            {
                const FStringView Name = Trim(Line);

                if (Name.empty())
                {
                    ParseError(Path, LineNumber,"Missing material name");
                }

                CurrentMaterial = GetOrAddMaterial(Info, MaterialLookup,Name);
            }
            else if (Prefix == "s")
            {
                const FStringView Group = TakeToken(Line);

                if (Group == "off" || Group == "0")
                {
                    CurrentSmoothingGroup = 0;
                }
                else if (Group == "on")
                {
                    CurrentSmoothingGroup = 1;
                }
                else
                {
                    CurrentSmoothingGroup = ReadNumber<uint32>(Group, Path, LineNumber);
                }

                RequireEnd(Line, Path, LineNumber);
            }
            else if (Prefix == "p"|| Prefix == "l"|| Prefix == "curv"|| Prefix == "curv2" || Prefix == "surf")
            {
                ParseError(Path, LineNumber,"Only polygon faces are supported");
            }

            // g 그룹 이름은 현재 수집하지 않는다.
        });

    if (Info.Triangles.IsEmpty())
    {
        ParseError(Path, 0, "No polygon faces found");
    }

    for (const FObjMaterialInfo& Material : Info.Materials)
    {
        if (!Material.bDefined)
        {
            ParseError(Path, 0,"Undefined material: " + Material.Name);
        }
    }
    // 객체 / 위치 / 스무딩 그룹 튜플을 키값으로 사용
    using FSmoothKey = std::tuple<int32, int32, uint32>;

    struct FSmoothNormal
    {
        FVector Sum{};
        int32 NormalIndex = -1;
    };
    std::map<FSmoothKey, FSmoothNormal> SmoothNormals;  //스무스가 켜졌을때의 노말벡터를 구하기위한 누적 노말들

    auto GetFaceNormal = [&](const FObjTriangle& Triangle)
        {
            const FVector& P0 = Info.Positions[Triangle.Corners[0].PositionIndex];
            const FVector& P1 = Info.Positions[Triangle.Corners[1].PositionIndex];
            const FVector& P2 = Info.Positions[Triangle.Corners[2].PositionIndex];

            return (P1 - P0).Cross(P2 - P0);
        };

    auto AddNormal = [&](FVector Normal)
        {
            // 현재 FVector::Normalize()는 0 벡터를 해결해 주지 않는다.
            if (Normal.LengthSquared() <= EPSILON * EPSILON)
            {
                Normal = FVector(0.0f, 0.0f, 1.0f);
            }
            else
            {
                Normal.Normalize();
            }

            const int32 NormalIndex = Info.Normals.Num();
            Info.Normals.Add(Normal);
            return NormalIndex;
        };

    // 1. 스무딩 법선을 먼저 전부 누적한다.
    for (const FObjTriangle& Triangle : Info.Triangles)
    {
        if (Triangle.SmoothingGroup == 0)
        {
            continue;
        }

        // 정규화 전의 외적을 더하면 면적 가중 방식이 된다.
        const FVector FaceNormal = GetFaceNormal(Triangle);

        for (const FObjVertexIndex& Corner : Triangle.Corners)
        {
            const FSmoothKey Key{Triangle.ObjectIndex,Corner.PositionIndex,Triangle.SmoothingGroup};
            SmoothNormals[Key].Sum += FaceNormal;
        }
    }

    // 2. 누적이 끝났으므로, 누락된 법선을 등록하고 인덱스를 연결한다.
    for (FObjTriangle& Triangle : Info.Triangles)
    {
        // Flat 법선은 이 삼각형 안에서만 공유한다.
        int32 FlatNormalIndex = -1;

        for (FObjVertexIndex& Corner : Triangle.Corners)
        {
            if (Corner.NormalIndex >= 0)
            {
                continue; // 파일에 있는 법선은 유지
            }

            if (Triangle.SmoothingGroup == 0)
            {
                if (FlatNormalIndex == -1)
                {
                    FlatNormalIndex = AddNormal(GetFaceNormal(Triangle));
                }

                Corner.NormalIndex = FlatNormalIndex;
            }
            else
            {
                const FSmoothKey Key{Triangle.ObjectIndex,Corner.PositionIndex,Triangle.SmoothingGroup};

                FSmoothNormal& Smooth = SmoothNormals.at(Key);

                // 같은 키의 법선은 최초 한 번만 등록한다.
                if (Smooth.NormalIndex == -1)
                {
                    Smooth.NormalIndex = AddNormal(Smooth.Sum);
                }

                Corner.NormalIndex = Smooth.NormalIndex;
            }
        }
    }
    return Info;
}


//FStaticMesh를 굽기위한 코드 시작
namespace
{
    // 위치가 같아도 UV나 법선이 다르면 별도의 렌더링 정점이다.
    struct FVertexKey
    {
        int32 PositionIndex;
        int32 UVIndex;
        int32 NormalIndex;

        bool operator==(const FVertexKey&) const = default;
    };

    struct FVertexKeyHash
    {
        size_t operator()(const FVertexKey& Key) const
        {
            size_t Hash = std::hash<int32>{}(Key.PositionIndex);
            Hash ^= std::hash<int32>{}(Key.UVIndex) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
            Hash ^= std::hash<int32>{}(Key.NormalIndex) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
            return Hash;
        }
    };

    FVertexPNCT MakeVertex(const FObjInfo& Info, const FObjVertexIndex& Corner)
    {
        const FVector& Position = Info.Positions[Corner.PositionIndex];
        // 법선 생성과 smoothing 처리는 Import에서 완료되어 있어야 한다.
        const FVector& Normal = Info.Normals[Corner.NormalIndex];
        const FVector4& Color = Info.VertexColors[Corner.PositionIndex];
        const FVector UV = Corner.UVIndex >= 0 ? Info.TexCoords[Corner.UVIndex] : FVector{};

        FVertexPNCT Vertex;
        Vertex.x = Position.X;
        Vertex.y = Position.Y;
        Vertex.z = Position.Z;
        Vertex.nx = Normal.X;
        Vertex.ny = Normal.Y;
        Vertex.nz = Normal.Z;
        Vertex.r = Color.X;
        Vertex.g = Color.Y;
        Vertex.b = Color.Z;
        Vertex.a = Color.W;
        // 좌표계를 맞춰준다.
        Vertex.u = UV.X;
        Vertex.v = 1.0f - UV.Y;
        return Vertex;
    }
}

// Import 결과를 렌더링용 정점·인덱스·섹션과 CPU 머티리얼 데이터로 변환한다.
FStaticMeshData FObjImporter::Cook(const FObjInfo& Info)
{
    // CPU 정점·인덱스 배열, Section과 Bounds를 구성한다.
    FStaticMeshData Result;

    // 경로를 UTF-8 문자열로 보존.
    const auto Utf8Path = Info.PathFileName.generic_u8string();
    Result.PathFileName = FString(Utf8Path.begin(), Utf8Path.end());

    // 순서를 유지하므로 기존 ObjectIndex를 그대로 사용할 수 있다.
    for (const FObjObjectInfo& Source : Info.Objects)
    {
        FStaticMeshObjectInfo Object;
        Object.Name = Source.Name;

        Result.Objects.Add(std::move(Object));
    }

    // 기존 MaterialIndex도 유지되도록 순서대로 복사.
    Result.Materials.Reserve(Info.Materials.Num());
    for (const FObjMaterialInfo& Source : Info.Materials)
    {
        // 기반 구조체를 값으로 복사하므로 문자열과 경로도 결과가 직접 소유한다.
        Result.Materials.Add(static_cast<const FStaticMeshMaterial&>(Source));
    }
    std::unordered_map<FVertexKey, uint32, FVertexKeyHash> VertexLookup;
    Result.Indices.Reserve(static_cast<size_t>(Info.Triangles.Num()) * 3);
    int32 DefaultMaterialIndex = -1;

    for (const FObjTriangle& Triangle : Info.Triangles)
    {
        int32 MaterialIndex = Triangle.MaterialIndex;
        if (MaterialIndex < 0)
        {
            // 기존 머티리얼 인덱스는 유지하고, 미지정 머티리얼은 기본 머티리얼 하나를 공유한다.
            if (DefaultMaterialIndex < 0)
            {
                DefaultMaterialIndex = Result.Materials.Num();
                Result.Materials.Add(FStaticMeshMaterial{});
            }
            MaterialIndex = DefaultMaterialIndex;
        }

        // 입력 순서를 유지하며 연속된 객체·머티리얼 범위를 하나의 Section으로 묶는다.
        if (Result.Sections.IsEmpty()
            || Result.Sections[Result.Sections.Num() - 1].ObjectIndex != Triangle.ObjectIndex
            || Result.Sections[Result.Sections.Num() - 1].MaterialIndex != static_cast<uint32>(MaterialIndex))
        {
            FMeshSection Section;
            Section.FirstIndex = static_cast<uint32>(Result.Indices.Num());
            Section.MaterialIndex = static_cast<uint32>(MaterialIndex);
            Section.ObjectIndex = Triangle.ObjectIndex;
            Result.Sections.Add(Section);
        }

        for (const FObjVertexIndex& Corner : Triangle.Corners)
        {
            const FVertexKey Key{ Corner.PositionIndex, Corner.UVIndex, Corner.NormalIndex };
            const auto [Iterator, bInserted] = VertexLookup.emplace(Key, static_cast<uint32>(Result.Vertices.Num()));
            if (bInserted)
            {
                const FVertexPNCT Vertex = MakeVertex(Info, Corner);
                const FVector Position{ Vertex.x, Vertex.y, Vertex.z };
                if (Result.Vertices.IsEmpty())
                {
                    Result.BoundsMin = Position;
                    Result.BoundsMax = Position;
                }
                else
                {
                    Result.BoundsMin.X = std::min(Result.BoundsMin.X, Position.X);
                    Result.BoundsMin.Y = std::min(Result.BoundsMin.Y, Position.Y);
                    Result.BoundsMin.Z = std::min(Result.BoundsMin.Z, Position.Z);
                    Result.BoundsMax.X = std::max(Result.BoundsMax.X, Position.X);
                    Result.BoundsMax.Y = std::max(Result.BoundsMax.Y, Position.Y);
                    Result.BoundsMax.Z = std::max(Result.BoundsMax.Z, Position.Z);
                }
                Result.Vertices.Add(Vertex);
            }
            Result.Indices.Add(Iterator->second);
        }

        Result.Sections[Result.Sections.Num() - 1].IndexCount += 3;
    }

    return Result;
}
