#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <iostream>

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

    for (const auto& relPath : files) {
        int pathLen = (int)relPath.length();
        fwrite(&pathLen, sizeof(int), 1, fpOut);

        fwrite(relPath.c_str(), 1, pathLen, fpOut);

        std::string fullPath = rootPath + relPath;
        FILE* fpIn;
        fopen_s(&fpIn, fullPath.c_str(), "rb");
        if (fpIn) {
            fseek(fpIn, 0, SEEK_END);
            int size = ftell(fpIn);
            fseek(fpIn, 0, SEEK_SET);

            std::vector<char> buffer(size);
            if (size > 0) fread(buffer.data(), 1, size, fpIn);
            fclose(fpIn);

            fwrite(&size, sizeof(int), 1, fpOut);
            if (size > 0) fwrite(buffer.data(), 1, size, fpOut);

            successCount++;
        }
        else {
            int zero = 0;
            fwrite(&zero, sizeof(int), 1, fpOut);
            printf("  [Error] Failed to read: %s\n", fullPath.c_str());
        }
    }

    fclose(fpOut);
    printf("Successfully packed %d/%d files.\n", successCount, count);
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
