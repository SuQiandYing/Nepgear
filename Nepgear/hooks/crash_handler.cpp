#include "../pch.h"
#include "crash_handler.h"
#include "utils.h"
#include <stdio.h>

namespace CrashHandler {

    LONG WINAPI UnhandledExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo) {
        Utils::Log(Utils::LOG_ERROR, "================ CRASH DETECTED ================");
        
        PEXCEPTION_RECORD pRecord = pExceptionInfo->ExceptionRecord;
        PCONTEXT pContext = pExceptionInfo->ContextRecord;

        Utils::Log(Utils::LOG_ERROR, "Exception Code:    0x%08X", pRecord->ExceptionCode);
        Utils::Log(Utils::LOG_ERROR, "Exception Address: 0x%p", pRecord->ExceptionAddress);

        HMODULE hModule = NULL;
        char moduleName[MAX_PATH] = "Unknown";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, 
            (LPCSTR)pRecord->ExceptionAddress, &hModule)) {
            GetModuleFileNameA(hModule, moduleName, MAX_PATH);
        }
        Utils::Log(Utils::LOG_ERROR, "Module:            %s", moduleName);

        Utils::Log(Utils::LOG_ERROR, "Registers:");
#ifdef _WIN64
        Utils::Log(Utils::LOG_ERROR, "RAX: 0x%016llX  RBX: 0x%016llX  RCX: 0x%016llX  RDX: 0x%016llX",
            pContext->Rax, pContext->Rbx, pContext->Rcx, pContext->Rdx);
        Utils::Log(Utils::LOG_ERROR, "RSI: 0x%016llX  RDI: 0x%016llX  RBP: 0x%016llX  RSP: 0x%016llX",
            pContext->Rsi, pContext->Rdi, pContext->Rbp, pContext->Rsp);
        Utils::Log(Utils::LOG_ERROR, "RIP: 0x%016llX  EFlags: 0x%08X",
            pContext->Rip, pContext->EFlags);
        Utils::Log(Utils::LOG_ERROR, "R8:  0x%016llX  R9:  0x%016llX  R10: 0x%016llX  R11: 0x%016llX",
            pContext->R8, pContext->R9, pContext->R10, pContext->R11);
        Utils::Log(Utils::LOG_ERROR, "R12: 0x%016llX  R13: 0x%016llX  R14: 0x%016llX  R15: 0x%016llX",
            pContext->R12, pContext->R13, pContext->R14, pContext->R15);
#else
        Utils::Log(Utils::LOG_ERROR, "EAX: 0x%08X  EBX: 0x%08X  ECX: 0x%08X  EDX: 0x%08X",
            pContext->Eax, pContext->Ebx, pContext->Ecx, pContext->Edx);
        Utils::Log(Utils::LOG_ERROR, "ESI: 0x%08X  EDI: 0x%08X  EBP: 0x%08X  ESP: 0x%08X",
            pContext->Esi, pContext->Edi, pContext->Ebp, pContext->Esp);
        Utils::Log(Utils::LOG_ERROR, "EIP: 0x%08X  EFlags: 0x%08X",
            pContext->Eip, pContext->EFlags);
#endif

        Utils::Log(Utils::LOG_ERROR, "================================================");
        
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void Install() {
        SetUnhandledExceptionFilter(UnhandledExceptionHandler);
        Utils::Log(Utils::LOG_INFO, "[CrashHandler] Installed Global Exception Filter.");
    }
}
