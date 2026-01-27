#include <windows.h>
#include <compressapi.h>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <chrono>
#include <iomanip>

#pragma comment(lib, "cabinet.lib")

namespace fs = std::filesystem;

DECOMPRESSOR_HANDLE g_decompressor = NULL;

void SetColor(int colorCode) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), colorCode);
}

std::wstring SmartToWide(const std::vector<char>& buffer) {
    if (buffer.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, buffer.data(), -1, NULL, 0);
    if (size > 0) {
        std::vector<wchar_t> wbuf(size);
        MultiByteToWideChar(CP_UTF8, 0, buffer.data(), -1, wbuf.data(), size);
        return std::wstring(wbuf.data());
    }
    size = MultiByteToWideChar(CP_ACP, 0, buffer.data(), -1, NULL, 0);
    if (size > 0) {
        std::vector<wchar_t> wbuf(size);
        MultiByteToWideChar(CP_ACP, 0, buffer.data(), -1, wbuf.data(), size);
        return std::wstring(wbuf.data());
    }
    return L"Unknown_Path";
}

bool DecompressLZMS(const std::vector<char>& input, std::vector<char>& output, size_t originalSize) {
    if (g_decompressor == NULL) {
        if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, NULL, &g_decompressor)) return false;
    }
    output.resize(originalSize);
    SIZE_T decompressedSize = 0;
    return Decompress(g_decompressor, input.data(), input.size(), output.data(), originalSize, &decompressedSize);
}

void DrawProgressBar(int current, int total, const std::wstring& currentFile) {
    const int barWidth = 30;
    float progress = (total > 0) ? (float)current / total : 1.0f;
    int pos = (int)(barWidth * progress);

    std::wcout << L"\r";
    SetColor(11); std::wcout << L"[";
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::wcout << L"=";
        else if (i == pos) std::wcout << L">";
        else std::wcout << L" ";
    }
    std::wcout << L"] ";
    SetColor(14); std::wcout << (int)(progress * 100.0) << L"% ";
    SetColor(7);  std::wcout << L"(" << current << L"/" << total << L") ";
    std::wstring displayFile = currentFile;
    if (displayFile.length() > 20) displayFile = L"..." + displayFile.substr(displayFile.length() - 17);
    SetColor(8);  std::wcout << std::left << std::setw(20) << displayFile;
    SetColor(7);
}

bool UnpackFile(const fs::path& packagePath) {
    auto startTime = std::chrono::high_resolution_clock::now();

    FILE* fpPack = nullptr;
    if (_wfopen_s(&fpPack, packagePath.c_str(), L"rb") != 0 || !fpPack) {
        SetColor(12);
        std::wcout << L"\n[错误] 无法打开: " << packagePath.wstring() << L"\n";
        return false;
    }

    int fileCount = 0;
    fread(&fileCount, sizeof(int), 1, fpPack);
    if (fileCount <= 0 || fileCount > 2000000) {
        std::wcout << L"无效的封包格式或文件已损坏。\n";
        fclose(fpPack);
        return false;
    }

    fs::path outDir = packagePath;
    outDir.replace_extension("");
    outDir += L"_Unpacked";
    fs::create_directories(outDir);

    std::wcout << L"正在解压: " << packagePath.filename().wstring() << L"\n";
    std::wcout << L"文件总数: " << fileCount << L"\n\n";

    for (int i = 0; i < fileCount; ++i) {
        int pathLen = 0;
        if (fread(&pathLen, sizeof(int), 1, fpPack) != 1) break;
        std::vector<char> pathBuf(pathLen + 1, 0);
        fread(pathBuf.data(), 1, pathLen, fpPack);
        std::wstring relPath = SmartToWide(pathBuf);
        fs::path fullPath = outDir / relPath;

        fs::create_directories(fullPath.parent_path());

        int size1 = 0, size2 = 0;
        fread(&size1, sizeof(int), 1, fpPack);

        long long posBeforeSize2 = _ftelli64(fpPack);
        fread(&size2, sizeof(int), 1, fpPack);

        bool isCompressedFormat = true;
        if (size2 > size1 || size2 < 0 || size1 < 0) {
            isCompressedFormat = false;
        }

        std::vector<char> outData;
        if (isCompressedFormat) {
            int originalSize = size1;
            int finalSize = size2;
            std::vector<char> fileData(finalSize);
            if (finalSize > 0) fread(fileData.data(), 1, finalSize, fpPack);

            if (finalSize < originalSize && finalSize > 0) {
                if (!DecompressLZMS(fileData, outData, originalSize)) {
                    outData = fileData;
                }
            }
            else {
                outData = fileData;
            }
        }
        else {
            _fseeki64(fpPack, posBeforeSize2, SEEK_SET);
            int realSize = size1;
            outData.resize(realSize);
            if (realSize > 0) fread(outData.data(), 1, realSize, fpPack);
        }

        FILE* fpOut = nullptr;
        if (_wfopen_s(&fpOut, fullPath.c_str(), L"wb") == 0 && fpOut) {
            if (!outData.empty()) fwrite(outData.data(), 1, outData.size(), fpOut);
            fclose(fpOut);
        }

        DrawProgressBar(i + 1, fileCount, relPath);
    }

    fclose(fpPack);
    std::wcout << L"\n\n";
    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    SetColor(10); std::wcout << L"任务完成!\n"; SetColor(7);
    std::wcout << L"耗时: " << std::fixed << std::setprecision(2) << elapsed.count() << L" 秒\n";
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    std::wcout.imbue(std::locale("", std::locale::all));
    SetConsoleTitleW(L"解包工具 (兼容旧版 & LZMS)");

    if (argc < 2) {
        SetColor(11);
        std::wcout << L"========================================\n";
        std::wcout << L"      VFS 解包工具     \n";
        std::wcout << L"========================================\n\n";
        SetColor(7);
        std::wcout << L"说明: 自动识别新旧两种封包格式。\n";
        std::wcout << L"使用: 将 .chs 文件拖入此程序。\n\n";
        system("pause");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        UnpackFile(argv[i]);
    }

    if (g_decompressor) CloseDecompressor(g_decompressor);
    std::wcout << L"\n所有任务已完成。";
    system("pause");
    return 0;
}