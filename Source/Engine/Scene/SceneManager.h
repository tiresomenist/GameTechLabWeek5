#pragma once

#include "Core/Container/String.h"

#include <filesystem>

struct FSceneType;
class UScene;

// 가장 최근 씬 로드 요청의 처리 상태를 나타낸다.
enum class ESceneLoadResult
{
	None,
	Pending,
	Succeeded,
	Failed
};

// Singleton
class GSceneManager
{
public:
	static GSceneManager* GetInstance();

	void Initialize();
	void Release();

	void Tick(float DeltaTime);

	void LoadScene(FSceneType* SceneType, FStringView SerializedName = "");
	// 파일 대화상자 등에서 얻은 전체 경로로 씬을 로드
	void LoadSceneFromPath(FSceneType* SceneType, const std::filesystem::path& ScenePath);
	void SaveScene(FStringView SerializedName);
	void SaveSceneToPath(const std::filesystem::path& ScenePath);

	UScene* GetScene() { return CurrentScene; };

	// 가장 최근 씬 로드 요청의 결과를 변경하지 않고 조회한다.
	ESceneLoadResult GetLastLoadResult() const { return LastLoadResult; }

private:

	void InternalLoadScene();
	void ClearNextScene();

	UScene* CurrentScene = nullptr;
	FSceneType* NextScene = nullptr;
	FString NextSceneFile = "";
	std::filesystem::path NextScenePath;

	// 싱글톤
	GSceneManager() = default;
	~GSceneManager() = default;
	GSceneManager(const GSceneManager&) = delete;
	GSceneManager& operator=(const GSceneManager&) = delete;

	ESceneLoadResult LastLoadResult = ESceneLoadResult::None;
};

