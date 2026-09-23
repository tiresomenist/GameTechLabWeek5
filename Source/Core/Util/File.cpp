#include "pch.h"
#include "File.h"
#include <Windows.h>
#include <shobjidl.h>
#include <filesystem>
#include <optional>
#include <system_error>

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <format>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

void File::WriteText(FStringView Path, FStringView Text)
{
    namespace fs = std::filesystem;
	const fs::path Target = fs::absolute(File::PathFromUtf8(Path));
	wchar_t TempName[MAX_PATH]{};
    if (!GetTempFileNameW(Target.parent_path().c_str(), L"scn", 0, TempName))
        throw std::system_error(GetLastError(), std::system_category());
    const fs::path Temp{TempName};
    try
    {
        std::ofstream Out;
        Out.exceptions(std::ios::failbit | std::ios::badbit);
        Out.open(Temp, std::ios::binary | std::ios::trunc);
        Out << Text;
        Out.flush();
        Out.close();
        if (!MoveFileExW(Temp.c_str(), Target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(GetLastError(), std::system_category());
    }
    catch (...)
    {
        std::error_code Ignored;
        fs::remove(Temp, Ignored);
        throw;
    }
}

void File::WriteTextToPath(const std::filesystem::path& Path, FStringView Text)
{
	File::WriteText(File::PathToUtf8(Path), Text);

}

FString File::ReadText(FStringView Path)
{
	FString PathString{ Path };

	std::ifstream In{ File::PathFromUtf8(Path) };
	std::stringstream StringStream;

	if (!In.is_open())
	{
		throw std::runtime_error(std::format("파일을 불러올 수 없습니다: {}", Path));
	}

	FString Text;

	StringStream << In.rdbuf();

	Text = StringStream.str();

	In.close();

	return Text;
}

FString File::ReadTextFromPath(const std::filesystem::path& Path)
{
	std::ifstream In{ Path, std::ios::binary };

	if (!In.is_open())
	{
		throw std::runtime_error("파일을 불러올 수 없습니다: " + File::PathToUtf8(Path));
	}

	std::stringstream StringStream;
	StringStream << In.rdbuf();

	return StringStream.str();
}

std::optional<std::filesystem::path> File::OpenFileDialog(HWND Owner, EFileDialogType Type, const std::filesystem::path& InitialDir)
{
	std::optional<std::filesystem::path> Result = std::nullopt;

	// S_OK / S_FALSE 면 짝을 맞춰 Uninit, RPC_E_CHANGED_MODE 면 건드리지 않음
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	const bool bNeedUninit = SUCCEEDED(hr);

	IFileOpenDialog* FileOpen = nullptr;
	hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&FileOpen));

	if (SUCCEEDED(hr))
	{
		COMDLG_FILTERSPEC Filters[] =
		{
			{ L"JSON Scene Files (*.json)", L"*.json" },
			{ L"Image Files (*.png;*.jpg;*.dds;*.tga)", L"*.png;*.jpg;*.jpeg;*.dds;*.tga" },
			{ L"All Files (*.*)",           L"*.*" },
			{ L"Wavefront OBJ Files (*.obj)", L"*.obj" }

		};
		FileOpen->SetFileTypes(ARRAYSIZE(Filters), Filters);

		int DefaultIndex = 1;
		switch (Type)
		{
		case EFileDialogType::Json: DefaultIndex = 1; break;
		case EFileDialogType::Image:DefaultIndex = 2; break;
		case EFileDialogType::All:	DefaultIndex = 3; break;
		case EFileDialogType::Obj:   DefaultIndex = 4; break;
		default: DefaultIndex = 3; break;
		}
		FileOpen->SetFileTypeIndex(DefaultIndex);
		if (!InitialDir.empty())
		{
			const std::filesystem::path AbsoluteDir = std::filesystem::absolute(InitialDir);
			IShellItem* FolderItem = nullptr;
			if (SUCCEEDED(SHCreateItemFromParsingName(AbsoluteDir.c_str(), nullptr, IID_PPV_ARGS(&FolderItem))))
			{
				FileOpen->SetFolder(FolderItem);
				FolderItem->Release();
			}
		}

		FileOpen->SetFileTypeIndex(DefaultIndex);
		// 취소 시 hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)
		hr = FileOpen->Show(Owner);

		if (SUCCEEDED(hr))
		{
			IShellItem* Item = nullptr;
			if (SUCCEEDED(FileOpen->GetResult(&Item)))
			{
				PWSTR FilePath = nullptr;
				if (SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH, &FilePath)))
				{
					Result = std::filesystem::path(FilePath);
					CoTaskMemFree(FilePath);
				}
				Item->Release();
			}
		}

		FileOpen->Release();
	}

	if (bNeedUninit)
	{
		CoUninitialize();
	}

	return Result;
}

std::optional<std::filesystem::path> File::SaveFileDialog(HWND Owner, EFileDialogType Type, const std::filesystem::path& InitialDir)
{
	std::optional<std::filesystem::path> Result = std::nullopt;

	// S_OK / S_FALSE 면 짝을 맞춰 Uninit, RPC_E_CHANGED_MODE 면 건드리지 않음
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	const bool bNeedUninit = SUCCEEDED(hr);

	IFileSaveDialog* FileSave = nullptr;
	hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&FileSave));

	if (SUCCEEDED(hr))
	{
		COMDLG_FILTERSPEC Filters[] =
		{
			{ L"JSON Scene Files (*.json)", L"*.json" },
			{ L"Image Files (*.png;*.jpg;*.dds;*.tga)", L"*.png;*.jpg;*.jpeg;*.dds;*.tga" },
			{ L"All Files (*.*)",           L"*.*" },
			{ L"Wavefront OBJ Files (*.obj)", L"*.obj" }

		};
		FileSave->SetFileTypes(ARRAYSIZE(Filters), Filters);

		int DefaultIndex = 1;
		switch (Type)
		{
		case EFileDialogType::Json: DefaultIndex = 1; break;
		case EFileDialogType::Image:DefaultIndex = 2; break;
		case EFileDialogType::All:	DefaultIndex = 3; break;
		case EFileDialogType::Obj:   DefaultIndex = 4; break;
		default: DefaultIndex = 3; break;
		}
		FileSave->SetFileTypeIndex(DefaultIndex);
		if (Type == EFileDialogType::Json)
		{
			FileSave->SetDefaultExtension(L"json");
		}
		if (!InitialDir.empty())
		{
			const std::filesystem::path AbsoluteDir = std::filesystem::absolute(InitialDir);
			IShellItem* FolderItem = nullptr;
			if (SUCCEEDED(SHCreateItemFromParsingName(AbsoluteDir.c_str(), nullptr, IID_PPV_ARGS(&FolderItem))))
			{
				FileSave->SetFolder(FolderItem);
				FolderItem->Release();
			}
		}

		FileSave->SetFileTypeIndex(DefaultIndex);
		// 취소 시 hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)
		hr = FileSave->Show(Owner);

		if (SUCCEEDED(hr))
		{
			IShellItem* Item = nullptr;
			if (SUCCEEDED(FileSave->GetResult(&Item)))
			{
				PWSTR FilePath = nullptr;
				if (SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH, &FilePath)))
				{
					Result = std::filesystem::path(FilePath);
					CoTaskMemFree(FilePath);
				}
				Item->Release();
			}
		}

		FileSave->Release();
	}

	if (bNeedUninit)
	{
		CoUninitialize();
	}

	return Result;
}
// 파일경로를 UTF-8 문자열로 변환
FString File::PathToUtf8(const std::filesystem::path& Path)
{
	// char8_t 문자열의 UTF-8 바이트를 FString에 보관합니다.
	const auto Utf8 = Path.generic_u8string();
	return FString(Utf8.begin(), Utf8.end());
}

// UTF-8 문자열을 파일 경로로 복구
std::filesystem::path File::PathFromUtf8(FStringView Text)
{
	// char8_t 문자열로 전달하여 UTF-8 해석을 명시합니다.
	const std::u8string Utf8(Text.begin(), Text.end());
	return std::filesystem::path(Utf8);
}
