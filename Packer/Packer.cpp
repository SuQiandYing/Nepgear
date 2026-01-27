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

COMPRESSOR_HANDLE g_compressor = NULL;

void SetColor(int colorCode) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), colorCode);
}

void SetCursorVisible(bool visible) {
    HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 100;
    info.bVisible = visible ? TRUE : FALSE;
    SetConsoleCursorInfo(consoleHandle, &info);
}

std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::vector<char> buf(size);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, buf.data(), size, NULL, NULL);
    return std::string(buf.data());
}

bool CompressData(const std::vector<char>& input, std::vector<char>& output) {
    if (g_compressor == NULL) {
        if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, NULL, &g_compressor)) return false;
        DWORD blockSize = 1024 * 1024;
        SetCompressorInformation(g_compressor, COMPRESS_INFORMATION_CLASS_BLOCK_SIZE, &blockSize, sizeof(blockSize));
    }

    SIZE_T compressedSize = 0;
    if (!Compress(g_compressor, input.data(), input.size(), NULL, 0, &compressedSize)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
    }

    output.resize(compressedSize);
    if (!Compress(g_compressor, input.data(), input.size(), output.data(), compressedSize, &compressedSize)) {
        return false;
    }
    output.resize(compressedSize);
    return true;
}

void DrawProgressBar(int current, int total, const std::wstring& currentFile) {
    const int barWidth = 30;
    float progress = (float)current / total;
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

bool PackDirectory(const fs::path& rootPath, const fs::path& outputPath) {
    auto startTime = std::chrono::high_resolution_clock::now();

    if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) {
        SetColor(12);
        std::wcout << L"\n[错误] 路径无效: " << rootPath.wstring() << L"\n";
        return false;
    }

    std::vector<fs::path> filePaths;
    for (const auto& entry : fs::recursive_directory_iterator(rootPath)) {
        if (entry.is_regular_file()) filePaths.push_back(entry.path());
    }

    if (filePaths.empty()) {
        std::wcout << L"文件夹为空。\n";
        return false;
    }

    FILE* fpOut;
    if (_wfopen_s(&fpOut, outputPath.c_str(), L"wb") != 0) {
        SetColor(12);
        std::wcout << L"\n[错误] 无法创建输出文件: " << outputPath.wstring() << L"\n";
        return false;
    }

    int count = (int)filePaths.size();
    fwrite(&count, sizeof(int), 1, fpOut);

    size_t totalOriginal = 0;
    size_t totalCompressed = 0;
    int processed = 0;

    std::wcout << L"目标文件: " << outputPath.filename().wstring() << L"\n";
    std::wcout << L"文件总数: " << count << L"\n\n";

    SetCursorVisible(false);

    for (const auto& filePath : filePaths) {
        processed++;

        std::wstring relPath = fs::relative(filePath, rootPath).wstring();
        std::string relPathUTF8 = WideToUtf8(relPath);
        int pathLen = (int)relPathUTF8.length();

        fwrite(&pathLen, sizeof(int), 1, fpOut);
        fwrite(relPathUTF8.c_str(), 1, pathLen, fpOut);

        std::vector<char> inputBuffer;
        std::vector<char> compressedBuffer;

        FILE* fpIn;
        _wfopen_s(&fpIn, filePath.c_str(), L"rb");

        int originalSize = 0;
        int finalSize = 0;
        bool compressed = false;

        if (fpIn) {
            fseek(fpIn, 0, SEEK_END);
            originalSize = (int)ftell(fpIn);
            fseek(fpIn, 0, SEEK_SET);

            inputBuffer.resize(originalSize);
            if (originalSize > 0) fread(inputBuffer.data(), 1, originalSize, fpIn);
            fclose(fpIn);

            if (originalSize > 64 && CompressData(inputBuffer, compressedBuffer)) {
                if (compressedBuffer.size() < (size_t)originalSize) {
                    compressed = true;
                }
            }

            finalSize = compressed ? (int)compressedBuffer.size() : originalSize;

            fwrite(&originalSize, sizeof(int), 1, fpOut);
            fwrite(&finalSize, sizeof(int), 1, fpOut);

            if (compressed) fwrite(compressedBuffer.data(), 1, finalSize, fpOut);
            else if (originalSize > 0) fwrite(inputBuffer.data(), 1, originalSize, fpOut);
        }
        else {
            int zero = 0;
            fwrite(&zero, sizeof(int), 1, fpOut);
            fwrite(&zero, sizeof(int), 1, fpOut);
        }

        totalOriginal += originalSize;
        totalCompressed += finalSize;

        DrawProgressBar(processed, count, relPath);
    }

    fclose(fpOut);
    SetCursorVisible(true);
    std::wcout << L"\n\n";

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;

    SetColor(10); std::wcout << L"任务完成!\n"; SetColor(7);
    std::wcout << L"耗时     : " << std::fixed << std::setprecision(2) << elapsed.count() << L" 秒\n";
    std::wcout << L"原始大小 : " << totalOriginal / 1024.0 / 1024.0 << L" MB\n";
    std::wcout << L"压缩大小 : " << totalCompressed / 1024.0 / 1024.0 << L" MB\n";
    SetColor(14);
    std::wcout << L"平均压缩率: " << (totalOriginal > 0 ? (double)totalCompressed / totalOriginal * 100.0 : 0) << L"%\n";
    SetColor(7);
    std::wcout << L"----------------------------------------\n";

    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    std::wcout.imbue(std::locale("", std::locale::all));
    SetConsoleTitleW(L"封包工具");

    if (argc < 2) {
        SetColor(11);
        std::wcout << L"========================================\n";
        std::wcout << L"      VFS Packer Tool       \n";
        std::wcout << L"========================================\n\n";
        SetColor(7);
        std::wcout << L"使用说明: 请将文件夹拖动到此程序图标上进行打包。\n\n";
        system("pause");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        fs::path inputPath = argv[i];
        fs::path outputPath = inputPath;

        if (!outputPath.has_filename()) {
            outputPath = outputPath.parent_path();
        }
        outputPath.replace_extension(L".chs");

        PackDirectory(inputPath, outputPath);
    }

    if (g_compressor) CloseCompressor(g_compressor);

    std::wcout << L"\n所有任务已结束。";
    system("pause");
    return 0;
}