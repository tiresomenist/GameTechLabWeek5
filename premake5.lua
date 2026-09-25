workspace "GameTechLabWeek5"
    configurations { "Debug", "Release" }
    startproject "GameTechlabWeek5"

local ProjectRoot = path.getabsolute(".")

-- 실행 프로젝트를 생성하고 공통 빌드 설정을 적용합니다.
local function ConfigureApplication(ProjectName, OutputDirectory)
    -- 프로젝트별 실행 파일과 중간 산출물 경로를 설정합니다.
    project(ProjectName)
        kind "WindowedApp"
        language "C++"
        cppdialect "C++20"
        characterset "Unicode"
        targetname(ProjectName)
        targetdir(OutputDirectory)
        objdir "obj/%{prj.name}/%{cfg.buildcfg}"
        debugdir(ProjectRoot)

        -- 기존 소스와 사전 컴파일 헤더를 공통으로 사용합니다.
        pchheader "pch.h"
        pchsource "Source/pch.cpp"
        includedirs { "./Source/" }
        externalincludedirs { "./ThirdParty/" }

        files {
            "**.h", "**.cpp", "**.hpp", "**.c",
            "**.rc", "**.ico", "Assets/**", "Scenes/**"
        }
        removefiles { "tests/**", "obj/**", "bin/**" }

        -- 런타임에 사용하는 셰이더와 OBJ 모델은 콘텐츠로 취급합니다.
        filter "files:**.hlsl"
            buildaction "None"
        filter "files:Assets/**.obj"
            buildaction "None"
        filter {}

        -- ImGui 소스는 기존 방식대로 프로젝트 PCH를 사용하지 않습니다.
        filter "files:**/ImGui/**.cpp"
            enablepch "Off"
        filter {}

        -- 기존 병렬 컴파일, 호출 규약, 인코딩 설정을 유지합니다.
        multiprocessorcompile "On"
        filter "toolset:msc*"
            callingconvention "Cdecl"
            buildoptions { "/utf-8" }
        filter {}

        -- Debug 구성에서는 디버깅 심벌을 생성합니다.
        filter "configurations:Debug"
            defines { "DEBUG" }
            symbols "On"

        -- 기존 Release 최적화 정책을 유지합니다.
	-- 실험용으로 optimize Speed로 바꿔봄
        filter "configurations:Release"
            defines { "NDEBUG" }
            optimize "Speed"
	    symbols "On"
            functionlevellinking "Off"
            intrinsics "Off"
            stringpooling "Off"
            linktimeoptimization "Off"

        filter { "configurations:Release", "toolset:msc*" }
            linkoptions { "/OPT:NOREF", "/OPT:NOICF" }
        filter {}

        -- 모든 빌드 구성에 공통 라이브러리를 연결합니다.
        links { "d3d11", "d3dcompiler", "dxgi", "user32", "gdi32" }
end

-- 기존 에디터 프로젝트의 이름과 출력 위치를 유지합니다.
ConfigureApplication("GameTechlabWeek4", "bin/%{cfg.buildcfg}")

-- Viewer 실행 파일을 별도로 생성하고 전용 빌드 정의를 추가합니다.
ConfigureApplication("ObjViewer", "bin/ObjViewer/%{cfg.buildcfg}")
defines { "OBJVIEWER_APP" }
