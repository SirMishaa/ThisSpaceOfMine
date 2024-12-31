// Copyright (C) 2024 Jérôme "SirLynix" Leclercq (lynix680@gmail.com)
// This file is part of the "This Space Of Mine" project
// For conditions of distribution and use, see copyright notice in LICENSE

#include <NazaraUtils/Prerequisites.hpp>

#ifdef NAZARA_PLATFORM_WINDOWS

#include <CommonLib/Utility/CrashHandlerWin32.hpp>
#include <Nazara/Core/Error.hpp>
#include <fmt/format.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <functional>
#include <fstream>

#if __has_include(<StackWalker.h>)
#define TSOM_HAS_STACKWALKER
#include <StackWalker.h>
#endif

namespace tsom
{
	namespace
	{
		using MiniDumpWriteDumpFn = BOOL(*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

		static MiniDumpWriteDumpFn MiniDumpWriteDump = nullptr;
		static CrashHandlerWin32* s_crashHandler = nullptr;

		struct HandleCloser
		{
			void operator()(HANDLE h) const
			{
				if (h != INVALID_HANDLE_VALUE)
					CloseHandle(h);
			}
		};

#ifdef TSOM_HAS_STACKWALKER
		struct CallbackWalker : StackWalker
		{
			using StackWalker::StackWalker;
			using StackWalker::CallstackEntryType;
			using StackWalker::CallstackEntry;

			using SymInitCallback = std::function<void(const char* searchPath, DWORD symOptions, const char* userName)>;
			using LoadModuleCallback = std::function<void(const char* img, const char* mod, DWORD64 baseAddr, DWORD size, DWORD result, const char* symType, const char* pdbName, ULONGLONG fileVersion)>;
			using CallstackEntryCallback = std::function<void(CallstackEntryType eType, CallstackEntry& entry)>;
			using DbgHelpErrCallback = std::function<void(const char* szFuncName, DWORD gle, DWORD64 addr)>;

			void OnSymInit(LPCSTR szSearchPath, DWORD symOptions, LPCSTR szUserName) override
			{
				if (symInitCallback)
					symInitCallback(szSearchPath, symOptions, szUserName);
			}

			void OnLoadModule(LPCSTR img, LPCSTR mod, DWORD64 baseAddr, DWORD size, DWORD result, LPCSTR symType, LPCSTR pdbName, ULONGLONG fileVersion) override
			{
				if (loadModuleCallback)
					loadModuleCallback(img, mod, baseAddr, size, result, symType, pdbName, fileVersion);
			}

			void OnCallstackEntry(CallstackEntryType eType, CallstackEntry& entry) override
			{
				if (callstackEntryCallback)
					callstackEntryCallback(eType, entry);
			}

			void OnDbgHelpErr(LPCSTR szFuncName, DWORD gle, DWORD64 addr) override
			{
				if (dbgHelpErrCallback)
					dbgHelpErrCallback(szFuncName, gle, addr);
			}

			SymInitCallback symInitCallback;
			LoadModuleCallback loadModuleCallback;
			CallstackEntryCallback callstackEntryCallback;
			DbgHelpErrCallback dbgHelpErrCallback;
		};
#endif

		using WinHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;
	}


	CrashHandlerWin32::~CrashHandlerWin32()
	{
		Uninstall();
		MiniDumpWriteDump = nullptr;
	}

	bool CrashHandlerWin32::Install()
	{
		if (!MiniDumpWriteDump)
		{
			if (!m_windbg.Load("Dbghelp.dll"))
			{
				fprintf(stderr, "failed to load Dbghelp.dll: %s\nCrashDump will not be generated.\n", m_windbg.GetLastError().data());
				return false;
			}

			MiniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(m_windbg.GetSymbol("MiniDumpWriteDump"));
			if (!MiniDumpWriteDump)
			{
				fprintf(stderr, "failed to load MiniDumpWriteDump or StackWalk64 symbol from Dbghelp.dll\nCrashDump will not be generated.\n");
				return false;
			}
		}

		NazaraAssertMsg(s_crashHandler == nullptr, "CrashHandler has been installed multiple times");
		s_crashHandler = this;
		::SetUnhandledExceptionFilter(HandleException);

		return true;
	}

	void CrashHandlerWin32::HandleUnhandledException(const std::exception* e)
	{
		std::array<wchar_t, MAX_PATH> filename = GetCrashdumpFilename();
		std::wcscat(filename.data(), L".log");

		std::string errorMessage = (e) ? fmt::format("Unhandled C++ exception {}: {}", typeid(*e).name(), e->what()) : "Unhandled unknown C++ exception";

		GenerateCrashlog(filename.data(), errorMessage, nullptr, GetCurrentThreadId());
	}

	void CrashHandlerWin32::Uninstall()
	{
		::SetUnhandledExceptionFilter(nullptr);
		s_crashHandler = nullptr;
	}

	void CrashHandlerWin32::GenerateCrashdump(const wchar_t* filename, EXCEPTION_POINTERS* e)
	{
		WinHandle dumpFile(CreateFileW(filename, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (dumpFile.get() == INVALID_HANDLE_VALUE)
		{
			std::fwprintf(stderr, L"CrashDump: failed to create file %ls\n", filename);
			return;
		}

		MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
		exceptionInfo.ThreadId = GetCurrentThreadId();
		exceptionInfo.ExceptionPointers = e;
		exceptionInfo.ClientPointers = FALSE;

		BOOL success = MiniDumpWriteDump(
			GetCurrentProcess(),
			GetCurrentProcessId(),
			dumpFile.get(),
			MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo),
			e ? &exceptionInfo : nullptr,
			nullptr,
			nullptr);

		if (success)
			std::fwprintf(stderr, L"Unhandled exception triggered: crashDump %ls generated\n", filename);
		else
			std::fprintf(stderr, "CrashDump: MiniDumpWriteDump failed: %u (%s)\n", GetLastError(), Nz::Error::GetLastSystemError().data());
	}

	void CrashHandlerWin32::GenerateCrashlog(const wchar_t* filename, std::string_view errorMessage, EXCEPTION_POINTERS* e, DWORD /*crashedThread*/)
	{
		std::fstream dumpFile(filename, std::ios_base::out | std::ios_base::trunc);
		if (!dumpFile)
		{
			std::fprintf(stderr, "CrashDump: failed to create file %ls\n", filename);
			return;
		}
		dumpFile << std::fixed;

		WriteHeader(dumpFile);
		if (!errorMessage.empty())
			dumpFile << errorMessage << "\n\n";

		dumpFile << std::flush;

#ifdef TSOM_HAS_STACKWALKER
		std::ostringstream callstackStream;
		callstackStream << std::fixed;

		std::ostringstream moduleStream;
		moduleStream << std::fixed;

		CallbackWalker stackLogger;
		stackLogger.symInitCallback = [&](const char* searchPath, DWORD symOptions, const char* /*userName*/)
		{
			moduleStream << "SymInit:\n";
			moduleStream << " - SearchPath: " << searchPath << "\n";
			moduleStream << " - SymOptions: " << symOptions << "\n";
			moduleStream << "\n";
			moduleStream << "Modules:\n";
		};

		stackLogger.loadModuleCallback = [&](const char* img, const char* mod, DWORD64 baseAddr, DWORD size, DWORD result, const char* symType, const char* pdbName, ULONGLONG fileVersion)
		{
			moduleStream << " - " << img << ":" << mod << " (" << reinterpret_cast<void*>(static_cast<std::uintptr_t>(baseAddr)) << ")";
			moduleStream << ", size: " << size << " (result: " << result << "), SymType: " << symType << ", PDB: " << pdbName;

			if (fileVersion != 0)
			{
				DWORD v4 = ((fileVersion >>  0) & 0xFFFF);
				DWORD v3 = ((fileVersion >> 16) & 0xFFFF);
				DWORD v2 = ((fileVersion >> 32) & 0xFFFF);
				DWORD v1 = ((fileVersion >> 48) & 0xFFFF);
				moduleStream << "fileVersion: " << v1 << "." << v2 << "." << v3 << "." << v4;
			}

			moduleStream << "\n";
		};

		stackLogger.callstackEntryCallback = [&](CallbackWalker::CallstackEntryType /*eType*/, CallbackWalker::CallstackEntry& entry)
		{
			const char* functionName;
			if (entry.undFullName[0])
				functionName = entry.undFullName;
			else if (entry.undName[0])
				functionName = entry.undName;
			else if (entry.name[0])
				functionName = entry.name;
			else
				functionName = nullptr;

			callstackStream << " - " << entry.moduleName << "!";
			if (functionName)
				callstackStream << functionName << '+' << std::hex << entry.offsetFromSmybol;
			else
				callstackStream << std::hex << entry.offset;

			if (entry.lineFileName[0])
				callstackStream << " (" << entry.lineFileName << ":" << std::dec << entry.lineNumber << ")";

			callstackStream << "\n";
		};

		/*stackLogger.dbgHelpErrCallback = [&](LPCSTR szFuncName, DWORD gle, DWORD64 addr)
		{
			callstackStream << "ERROR: " << szFuncName << ", GetLastError: " << std::hex << gle << " (Address: " << reinterpret_cast<void*>(static_cast<std::uintptr_t>(addr)) << ")\n";
		};*/

		stackLogger.ShowCallstack(GetCurrentThread(), e ? e->ContextRecord : nullptr);

		dumpFile << "Callstack:\n";
		dumpFile << callstackStream.str();
		dumpFile << "\n\n";
		dumpFile << "Modules info:\n";
		dumpFile << moduleStream.str();
#else
		NazaraUnused(e);
		dumpFile << "StackWalker support not built, no callstack and module info could be generated.\n";
#endif

		dumpFile.close();
		std::fprintf(stderr, "Unhandled exception triggered: Callstack file %ls generated\n", filename);
	}

	std::array<wchar_t, MAX_PATH> CrashHandlerWin32::GetCrashdumpFilename()
	{
		const wchar_t* executableFilename = L"unknown";

		std::array<wchar_t, MAX_PATH> executablePath;
		if (::GetModuleFileNameW(nullptr, executablePath.data(), DWORD(executablePath.size())) != 0)
		{
			if (auto it = std::find(executablePath.rbegin(), executablePath.rend(), L'\\'); it != executablePath.rend())
				executableFilename = &*it + 1;
		}

		SYSTEMTIME currentTime;
		::GetLocalTime(&currentTime);

		std::array<wchar_t, MAX_PATH> filename;
		int dumpFilenameLength = std::swprintf(filename.data(), filename.size(), L"%ls_crashdump_%04u%02u%02u_%02u%02u%02u", executableFilename, currentTime.wYear, currentTime.wMonth, currentTime.wDay, currentTime.wHour, currentTime.wMinute, currentTime.wSecond);
		if (dumpFilenameLength < 0)
		{
			static const wchar_t defaultFilename[] = L"crashdump_fmterror";
			constexpr std::size_t defaultFilenameLength = sizeof(defaultFilename) / sizeof(wchar_t);

			static_assert(filename.size() > defaultFilenameLength);
			std::memcpy(filename.data(), defaultFilename, defaultFilenameLength);
			dumpFilenameLength = defaultFilenameLength;
		}

		return filename;
	}

	std::string CrashHandlerWin32::GetErrorMessage(EXCEPTION_RECORD* record)
	{
		switch (record->ExceptionCode)
		{
			case EXCEPTION_ACCESS_VIOLATION:
				return fmt::format("EXCEPTION_ACCESS_VIOLATION: The thread tried to {0} virtual address {1:#016x} for which it does not have the appropriate access.",
					record->ExceptionInformation[0] == 1 ? "write" : "read",
					record->ExceptionInformation[1]);

			case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED: The thread tried to access an array element that is out of bounds and the underlying hardware supports bounds checking.";
			case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT: A breakpoint was encountered.";
			case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT: The thread tried to read or write data that is misaligned on hardware that does not provide alignment.";
			case EXCEPTION_FLT_DENORMAL_OPERAND:     return "EXCEPTION_FLT_DENORMAL_OPERAND: One of the operands in a floating-point operation is denormal.";
			case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO: The thread tried to divide a floating-point value by a floating-point divisor of zero.";
			case EXCEPTION_FLT_INEXACT_RESULT:       return "EXCEPTION_FLT_INEXACT_RESULT: The result of a floating-point operation cannot be represented exactly as a decimal fraction.";
			case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION: This exception represents any floating-point exception not included in this list.";
			case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW: The exponent of a floating-point operation is greater than the magnitude allowed by the corresponding type.";
			case EXCEPTION_FLT_STACK_CHECK:          return "EXCEPTION_FLT_STACK_CHECK: The stack overflowed or underflowed as the result of a floating-point operation.";
			case EXCEPTION_FLT_UNDERFLOW:            return "EXCEPTION_FLT_UNDERFLOW: The exponent of a floating-point operation is less than the magnitude allowed by the corresponding type.";
			case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION: The thread tried to execute an invalid instruction.";
			case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR: The thread tried to access a page that was not present, and the system was unable to load the page..";
			case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO: The thread tried to divide an integer value by an integer divisor of zero.";
			case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW: The result of an integer operation caused a carry out of the most significant bit of the result.";
			case EXCEPTION_INVALID_DISPOSITION:      return "EXCEPTION_INVALID_DISPOSITION: An exception handler returned an invalid disposition to the exception dispatcher.";
			case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION: The thread tried to continue execution after a noncontinuable exception occurred.";
			case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION: The thread tried to execute an instruction whose operation is not allowed in the current machine mode.";
			case EXCEPTION_SINGLE_STEP:              return "EXCEPTION_SINGLE_STEP: A trace trap or other single-instruction mechanism signaled that one instruction has been executed.";
			case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW: The thread used up its stack.";
		}

		return fmt::format("EXCEPTION_UNKNOWN: an unknown exception code {} has been raised", record->ExceptionCode);
	}

	LONG CALLBACK CrashHandlerWin32::HandleException(EXCEPTION_POINTERS* e)
	{
		if (::IsDebuggerPresent())
			return EXCEPTION_CONTINUE_SEARCH;

		std::array<wchar_t, MAX_PATH> filename = GetCrashdumpFilename();
		std::size_t filenameLength = std::wcslen(filename.data());

		std::memcpy(&filename[filenameLength], L".dmp", 5 * sizeof(wchar_t));
		s_crashHandler->GenerateCrashdump(filename.data(), e);

		std::string errorMessage = GetErrorMessage(e->ExceptionRecord);

		std::memcpy(&filename[filenameLength], L".log", 5 * sizeof(wchar_t));
		s_crashHandler->GenerateCrashlog(filename.data(), "Unhandled SEH " + errorMessage, e, GetCurrentThreadId());

		return EXCEPTION_EXECUTE_HANDLER;
	}
}

#endif
