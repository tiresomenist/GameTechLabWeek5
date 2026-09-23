#include "pch.h"
#include "SceneManager.h"
#include "Core/Container/String.h"
#include "Core/Serialization/Archive.h"
#include "Core/Serialization/JsonReader.h"
#include "Core/Serialization/JsonWriter.h"
#include "Engine/Object/ObjectStatics.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Log.h"
#include "Engine/Scene/SceneValidation.h"
#include <filesystem>
#include <stdexcept>
#include <memory>

namespace
{
	constexpr FStringView SceneDirectory = "Scenes";

	FString GetScenePath(FStringView SceneName)
    {
        FString Name{SceneName};
        if (Name.empty() || Name.back() == '.' || Name.back() == ' ' ||
            Name.find_first_of("\\/:*?\"<>|") != FString::npos)
            throw std::runtime_error("Invalid scene name");
        for (unsigned char C : Name)
            if (C < 32) throw std::runtime_error("Invalid scene name");
        FString Base = Name.substr(0, Name.find('.'));
        for (char& C : Base) if (C >= 'a' && C <= 'z') C -= ('a' - 'A');
        const bool Numbered = Base.size() == 4 &&
            (Base.starts_with("COM") || Base.starts_with("LPT")) && Base[3] >= '1' && Base[3] <= '9';
        if (Base == "CON" || Base == "PRN" || Base == "AUX" || Base == "NUL" || Numbered)
            throw std::runtime_error("Reserved scene name");
        return (std::filesystem::path(SceneDirectory) / (Name + ".json")).generic_string();
    }
}

GSceneManager* GSceneManager::GetInstance()
{
    static GSceneManager Instance{};
    return &Instance;
}

void GSceneManager::Initialize()
{
	LoadScene(UScene::GetStaticSceneType(), "");
}

void GSceneManager::Release()
{
    ClearNextScene();
    LastLoadResult = ESceneLoadResult::None;

	if (CurrentScene)
	{
		CurrentScene->EndPlay();

		delete CurrentScene;
		CurrentScene = nullptr;
	}
}

void GSceneManager::Tick(float DeltaTime)
{
    if (CurrentScene)
    {
        CurrentScene->Tick(DeltaTime);
    }

    // 타입 포인터가 잘못된 요청도 검사 후 Failed로 종료시킨다.
    if (LastLoadResult == ESceneLoadResult::Pending)
    {
        InternalLoadScene();
    }
}

void GSceneManager::LoadScene(FSceneType* SceneType, FStringView SerializedName)
{
	NextScene = SceneType;
	NextSceneFile = SerializedName;
	NextScenePath.clear();
    LastLoadResult = ESceneLoadResult::Pending;

	if (!CurrentScene)
	{
		InternalLoadScene();
	}
}

void GSceneManager::LoadSceneFromPath(FSceneType* SceneType, const std::filesystem::path& ScenePath)
{
	NextScene = SceneType;
	NextSceneFile.clear();
	NextScenePath = ScenePath;
    LastLoadResult = ESceneLoadResult::Pending;

	if (!CurrentScene)
	{
		InternalLoadScene();
	}
}

void GSceneManager::ClearNextScene()
{
	NextScene = nullptr;
	NextSceneFile.clear();
	NextScenePath.clear();
}

// 씬 파일을 검사한 뒤 기존 씬을 교체하고 저장된 객체를 복원한다.
void GSceneManager::InternalLoadScene()
{
    if (NextScene == nullptr || NextScene->SceneConstructor == nullptr)
    {
        LastLoadResult = ESceneLoadResult::Failed;
        ClearNextScene();
        return;
    }

    std::unique_ptr<FJsonReader> Reader;
    uint32 NextUUID = 0;

    // 기존 씬을 제거하기 전에 파일 읽기와 기본 형식 검사를 완료한다.
    try
    {
        // 파일 경로가 있으면 파일에서 생성하고, 없으면 빈 씬 Reader를 생성한다.
        if (!NextScenePath.empty())
        {
            Reader = FJsonReader::FromFile(NextScenePath);
        }
        else if (!NextSceneFile.empty())
        {
            const std::filesystem::path Path(GetScenePath(NextSceneFile));
            Reader = FJsonReader::FromFile(Path);
        }
        else
        {
            Reader = std::make_unique<FJsonReader>(R"({"Version":1,"NextUUID":0,"Actors":{},"Components":{}})");
        }
        NextUUID = ValidateSceneArchive(*Reader);
        Reader->ResetToRoot();
    }
    catch (const std::exception& Error)
    {
        UE_LOG("[SceneManager] Scene file validation failed: {}", Error.what());
        LastLoadResult = ESceneLoadResult::Failed;
        ClearNextScene();
        return;
    }
    const uint32 PreviousNextUUID = GObjectStatics::GetNextUUID(EObjectDomain::EOT_Scene);
    std::unique_ptr<UScene> Candidate;
    try
    {
        // 기존 씬을 유지한 채 새 씬의 복원을 끝낸다.
        GObjectStatics::SetNextUUID(EObjectDomain::EOT_Scene, NextUUID);
        Candidate.reset(NextScene->SceneConstructor());
        if (!Candidate){ throw std::runtime_error("Failed to create scene."); }

        Candidate->Serialize(*Reader);
        Candidate->BeginPlay();
    }
    catch (const std::exception& Error)
    {
        // 후보 씬과 후보 생성에 사용한 UUID 상태만 되돌린다.
        if (Candidate) Candidate->EndPlay();
        Candidate.reset();
        GObjectStatics::SetNextUUID(EObjectDomain::EOT_Scene, PreviousNextUUID);
        UE_LOG("[SceneManager] Scene restoration failed: {}", Error.what());
        LastLoadResult = ESceneLoadResult::Failed;
        ClearNextScene();
        return;
    }

    // 기본 검사를 통과한 뒤 기존 씬을 정리한다.
    if (CurrentScene)
    {
        CurrentScene->EndPlay();
        delete CurrentScene;
    }

    CurrentScene = Candidate.release();
    LastLoadResult = ESceneLoadResult::Succeeded;
    ClearNextScene();
}

// 현재 씬을 JSON Archive에 기록하고 검증 후 파일로 저장한다.
void GSceneManager::SaveScene(FStringView SerializedName)
{
    if (!CurrentScene || SerializedName.empty()) return;

    try
    {
        const FString FileName = GetScenePath(SerializedName);
        FJsonWriter Writer;

        // 파일 전체에 대한 버전과 다음 UUID를 루트에 기록한다.
        int32 Version = 1;
        uint32 NextUUID = GObjectStatics::GetNextUUID(EObjectDomain::EOT_Scene);
        Writer.Field("Version", Version);
        Writer.Field("NextUUID", NextUUID);
        CurrentScene->Serialize(Writer);

        // 완성된 메모리상의 문서를 검사한 뒤 실제 파일에 저장한다.
        FJsonReader ValidationReader(Writer.ToString());
        ValidateSceneArchive(ValidationReader);

        std::filesystem::create_directories(SceneDirectory);
        Writer.SaveToFile(FileName);
    }
    catch (const std::exception& Error)
    {
        UE_LOG("[SceneManager] Save {} failed: {}", SerializedName, Error.what());
    }
}

void GSceneManager::SaveSceneToPath(const std::filesystem::path& ScenePath)
{
    if (!CurrentScene || ScenePath.empty()) return;

    try
    {
        FJsonWriter Writer;

        // 파일 전체에 대한 버전과 다음 UUID를 루트에 기록한다.
        int32 Version = 1;
        uint32 NextUUID = GObjectStatics::GetNextUUID(EObjectDomain::EOT_Scene);
        Writer.Field("Version", Version);
        Writer.Field("NextUUID", NextUUID);
        CurrentScene->Serialize(Writer);

        // 완성된 메모리상의 문서를 검사한 뒤 실제 파일에 저장한다.
        FJsonReader ValidationReader(Writer.ToString());
        ValidateSceneArchive(ValidationReader);

        std::filesystem::create_directories(SceneDirectory);
        Writer.SaveToFilePath(ScenePath);
    }
    catch (const std::exception& Error)
    {
        UE_LOG("[SceneManager] Save {} failed: {}", ScenePath.string(), Error.what());
    }
}
