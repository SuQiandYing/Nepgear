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

void HideCursor() {
    HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 100;
    info.bVisible = FALSE;
    SetConsoleCursorInfo(consoleHandle, &info);
}

void ShowCursor() {
    HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 100;
    info.bVisible = TRUE;
    SetConsoleCursorInfo(consoleHandle, &info);
}

void SetColor(int colorCode) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), colorCode);
}

std::string AnsiToUtf8(const std::string& str) {
    if (str.empty()) return "";
    int wLen = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
    if (wLen <= 0) return str;
    std::vector<wchar_t> wBuf(wLen);
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, wBuf.data(), wLen);
    int uLen = WideCharToMultiByte(CP_UTF8, 0, wBuf.data(), -1, NULL, 0, NULL, NULL);
    if (uLen <= 0) return str;
    std::vector<char> uBuf(uLen);
    WideCharToMultiByte(CP_UTF8, 0, wBuf.data(), -1, uBuf.data(), uLen, NULL, NULL);
    return std::string(uBuf.data());
}

bool CompressData(const std::vector<char>& input, std::vector<char>& output) {
    static COMPRESSOR_HANDLE compressor = NULL;
    static bool initialized = false;

    if (!initialized) {
        if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, NULL, &compressor)) return false;
        DWORD blockSize = 1024 * 1024; // 1MB Block
        SetCompressorInformation(compressor, COMPRESS_INFORMATION_CLASS_BLOCK_SIZE, &blockSize, sizeof(blockSize));
        initialized = true;
    }

    SIZE_T compressedSize = 0;
    if (!Compress(compressor, input.data(), input.size(), NULL, 0, &compressedSize)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
    }

    output.resize(compressedSize);
    if (!Compress(compressor, input.data(), input.size(), output.data(), compressedSize, &compressedSize)) {
        return false;
    }

    output.resize(compressedSize);
    return true;
}

void DrawProgressBar(int current, int total, const std::string& currentFile, size_t compressedBytes, size_t originalBytes) {
    const int barWidth = 40;
    float progress = (float)current / total;
    int pos = (int)(barWidth * progress);

    std::cout << "\r";

    SetColor(11); 
    std::cout << "[";
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] ";

    SetColor(14); 
    std::cout << int(progress * 100.0) << "% ";

    SetColor(7); 
    std::cout << "(" << current << "/" << total << ") ";

    std::string displayFile = currentFile;
    if (displayFile.length() > 25) {
        displayFile = "..." + displayFile.substr(displayFile.length() - 22);
    }
    SetColor(8);
    std::cout << std::left << std::setw(26) << displayFile;

    SetColor(7);
}

bool PackDirectory(const std::string& inputFolder, const std::string& outputFile) {
    auto startTime = std::chrono::high_resolution_clock::now();

    fs::path rootPath(inputFolder);
    if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) {
        SetColor(12);
        std::cout << "[Error] Invalid directory: " << inputFolder << "\n";
        return false;
    }

    std::cout << "Scanning files...\n";
    std::vector<fs::path> filePaths;
    for (const auto& entry : fs::recursive_directory_iterator(rootPath)) {
        if (entry.is_regular_file()) {
            filePaths.push_back(entry.path());
        }
    }

    if (filePaths.empty()) {
        std::cout << "Folder is empty.\n";
        return false;
    }

    FILE* fpOut;
    if (fopen_s(&fpOut, outputFile.c_str(), "wb") != 0) {
        SetColor(12);
        std::cout << "[Error] Cannot create: " << outputFile << "\n";
        return false;
    }

    int count = (int)filePaths.size();
    fwrite(&count, sizeof(int), 1, fpOut);

    size_t totalOriginal = 0;
    size_t totalCompressed = 0;
    int processed = 0;

    std::cout << "Target: " << outputFile << "\n";
    std::cout << "Count : " << count << " files\n\n";

    HideCursor();

    for (const auto& filePath : filePaths) {
        processed++;

        fs::path relativePath = fs::relative(filePath, rootPath);
        std::string relPathStr = relativePath.string(); 

        std::string relPathUTF8 = AnsiToUtf8(relPathStr);

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
            originalSize = ftell(fpIn);
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

        DrawProgressBar(processed, count, relPathStr, totalCompressed, totalOriginal);
    }

    fclose(fpOut);
    ShowCursor();
    std::cout << "\n\n";

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    double ratio = totalOriginal > 0 ? (double)totalCompressed / totalOriginal * 100.0 : 0.0;

    SetColor(10); 
    std::cout << "Done!\n";
    SetColor(7);
    printf("Time Taken : %.2f seconds\n", elapsed.count());
    printf("Orig Size  : %.2f MB\n", totalOriginal / 1024.0 / 1024.0);
    printf("Comp Size  : %.2f MB\n", totalCompressed / 1024.0 / 1024.0);
    SetColor(14);
    printf("Comp Ratio : %.2f%%\n", ratio);
    SetColor(7);
    printf("----------------------------------------\n");

    return true;
}

int main(int argc, char* argv[]) {
    SetConsoleTitleA("Ultimate VFS Packer (UTF-8 Mode)");
    SetColor(7);

    if (argc < 2) {
        SetColor(11);
        printf("========================================\n");
        printf("      VFS Packer Tool v2.0 (UTF-8)      \n");
        printf("========================================\n\n");
        SetColor(7);
        printf("Usage: Drag folder(s) onto this EXE.\n\n");
        system("pause");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        std::string inputPath = argv[i];
        std::string outputPath = inputPath;

        if (outputPath.back() == '\\' || outputPath.back() == '/') {
            outputPath.pop_back();
        }
        outputPath += ".chs";

        PackDirectory(inputPath, outputPath);
    }

    std::cout << "\nAll tasks finished. ";
    system("pause");
    return 0;
}