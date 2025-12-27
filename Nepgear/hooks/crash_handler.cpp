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
        Utils::Log(Utils::LOG_ERROR, "EAX: 0x%08X  EBX: 0x%08X  ECX: 0x%08X  EDX: 0x%08X",
            pContext->Eax, pContext->Ebx, pContext->Ecx, pContext->Edx);
        Utils::Log(Utils::LOG_ERROR, "ESI: 0x%08X  EDI: 0x%08X  EBP: 0x%08X  ESP: 0x%08X",
            pContext->Esi, pContext->Edi, pContext->Ebp, pContext->Esp);
        Utils::Log(Utils::LOG_ERROR, "EIP: 0x%08X  EFlags: 0x%08X",
            pContext->Eip, pContext->EFlags);

        Utils::Log(Utils::LOG_ERROR, "================================================");
        
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void Install() {
        SetUnhandledExceptionFilter(UnhandledExceptionHandler);
        Utils::Log(Utils::LOG_INFO, "[CrashHandler] Installed Global Exception Filter.");
    }
}
