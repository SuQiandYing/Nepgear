#include <windows.h>
#include <compressapi.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <iostream>

#pragma comment(lib, "cabinet.lib")

bool CompressData(const std::vector<char>& input, std::vector<char>& output) {
    COMPRESSOR_HANDLE compressor = NULL;
    if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, NULL, &compressor)) {
        printf("  [Debug] CreateCompressor failed: %d\n", GetLastError());
        return false;
    }

    DWORD blockSize = 1024 * 1024; 
    SetCompressorInformation(compressor, COMPRESS_INFORMATION_CLASS_BLOCK_SIZE, &blockSize, sizeof(blockSize));

    SIZE_T compressedSize = 0;
    if (!Compress(compressor, input.data(), input.size(), NULL, 0, &compressedSize)) {
        DWORD error = GetLastError();
        if (error != ERROR_INSUFFICIENT_BUFFER) {
            printf("  [Debug] Compress (size query) failed: %d\n", error);
            CloseCompressor(compressor);
            return false;
        }
    }

    output.resize(compressedSize);
    if (!Compress(compressor, input.data(), input.size(), output.data(), compressedSize, &compressedSize)) {
        printf("  [Debug] Compress (data) failed: %d\n", GetLastError());
        CloseCompressor(compressor);
        return false;
    }

    output.resize(compressedSize);

    CloseCompressor(compressor);
    return true;
}

void GetAllFiles(std::string root, std::string currentSub, std::vector<std::string>& fileList) {
    std::string searchPath = root + currentSub + "*.*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);

    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        std::string relPath = currentSub + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            GetAllFiles(root, relPath + "\\", fileList);
        }
        else {
            fileList.push_back(relPath);
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
}

bool PackDirectory(std::string inputFolder, std::string outputFile) {
    printf("------------------------------------------------\n");
    printf("Processing: %s\n", inputFolder.c_str());

    std::string rootPath = inputFolder;
    if (rootPath.back() != '\\' && rootPath.back() != '/') {
        rootPath += "\\";
    }

    std::vector<std::string> files;
    GetAllFiles(rootPath, "", files);

    if (files.empty()) {
        printf("[Warning] Empty folder or invalid path, skipping.\n");
        return false;
    }

    printf("Found %d files. Packing to: %s\n", (int)files.size(), outputFile.c_str());

    FILE* fpOut;
    if (fopen_s(&fpOut, outputFile.c_str(), "wb") != 0) {
        printf("[Error] Cannot create output file: %s\n", outputFile.c_str());
        return false;
    }

    int count = (int)files.size();
    fwrite(&count, sizeof(int), 1, fpOut);

    int successCount = 0;
    size_t totalOriginal = 0;
    size_t totalCompressed = 0;

    for (const auto& relPath : files) {
        int pathLen = (int)relPath.length();
        fwrite(&pathLen, sizeof(int), 1, fpOut);
        fwrite(relPath.c_str(), 1, pathLen, fpOut);

        std::string fullPath = rootPath + relPath;
        FILE* fpIn;
        fopen_s(&fpIn, fullPath.c_str(), "rb");
        if (fpIn) {
            fseek(fpIn, 0, SEEK_END);
            int originalSize = ftell(fpIn);
            fseek(fpIn, 0, SEEK_SET);

            std::vector<char> inputBuffer(originalSize);
            if (originalSize > 0) fread(inputBuffer.data(), 1, originalSize, fpIn);
            fclose(fpIn);

            std::vector<char> compressedBuffer;
            bool compressed = false;
            
            if (originalSize > 64) {
                if (CompressData(inputBuffer, compressedBuffer)) {
                    if (compressedBuffer.size() < (size_t)originalSize) {
                        compressed = true;
                    }
                }
            }

            int finalToWriteSize = compressed ? (int)compressedBuffer.size() : originalSize;
            
            printf("  [%s] %s: %d -> %d bytes\n", 
                compressed ? "COMPRESSED" : "RAW", 
                relPath.c_str(), 
                originalSize, 
                finalToWriteSize);

            fwrite(&originalSize, sizeof(int), 1, fpOut);
            fwrite(&finalToWriteSize, sizeof(int), 1, fpOut);

            if (compressed) {
                fwrite(compressedBuffer.data(), 1, finalToWriteSize, fpOut);
            } else {
                if (originalSize > 0) fwrite(inputBuffer.data(), 1, originalSize, fpOut);
            }

            totalOriginal += originalSize;
            totalCompressed += finalToWriteSize;
            successCount++;
        }
        else {
            int zero = 0;
            fwrite(&zero, sizeof(int), 1, fpOut);
            fwrite(&zero, sizeof(int), 1, fpOut);
            printf("  [Error] Failed to read: %s\n", fullPath.c_str());
        }
    }

    fclose(fpOut);
    double ratio = totalOriginal > 0 ? (double)totalCompressed / totalOriginal * 100.0 : 100.0;
    printf("Successfully packed %d/%d files.\n", successCount, count);
    printf("Compression Summary: %I64u -> %I64u bytes (%.2f%%)\n", (unsigned __int64)totalOriginal, (unsigned __int64)totalCompressed, ratio);
    return true;
}

int main(int argc, char* argv[]) {
    SetConsoleTitleA("Folder Packer");

    if (argc < 2) {
        printf("Usage:\n");
        printf("  1. Drag and drop folders onto this EXE.\n");
        printf("  2. Command line: Packer.exe <Folder1> [Folder2] ...\n");
        system("pause");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        std::string inputPath = argv[i];

        DWORD attrib = GetFileAttributesA(inputPath.c_str());
        if (attrib == INVALID_FILE_ATTRIBUTES || !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
            printf("[Skip] '%s' is not a valid directory.\n", inputPath.c_str());
            continue;
        }

        std::string outputPath = inputPath;
        if (outputPath.back() == '\\' || outputPath.back() == '/') {
            outputPath.pop_back();
        }
        outputPath += ".chs";

        PackDirectory(inputPath, outputPath);
    }

    printf("\nAll tasks finished.\n");
    system("pause");
    return 0;
}
