#include <stdio.h>
#include <string.h>
#include "pin.h"

// all our windows stuff.. needs it's own namespace
namespace W {
    #include <windows.h>
}

#define USHORT W::USHORT
#define ULONG W::ULONG
typedef wchar_t *PWCH;
typedef char *PCHAR;
#define HANDLE W::HANDLE
typedef void *PVOID;
#define RTL_MAX_DRIVE_LETTERS 32

typedef struct _STRING {
    USHORT Length;
    USHORT MaximumLength;
    PCHAR Buffer;
} STRING, *PSTRING, ANSI_STRING, *PANSI_STRING, OEM_STRING, *POEM_STRING;

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWCH Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

typedef struct _CURDIR {
    UNICODE_STRING DosPath;
    HANDLE Handle;
} CURDIR, *PCURDIR;

typedef struct _RTL_DRIVE_LETTER_CURDIR {
    USHORT Flags;
    USHORT Length;
    ULONG TimeStamp;
    STRING DosPath;
} RTL_DRIVE_LETTER_CURDIR, *PRTL_DRIVE_LETTER_CURDIR;

typedef struct _RTL_USER_PROCESS_PARAMETERS {
    ULONG MaximumLength;
    ULONG Length;

    ULONG Flags;
    ULONG DebugFlags;

    HANDLE ConsoleHandle;
    ULONG ConsoleFlags;
    HANDLE StandardInput;
    HANDLE StandardOutput;
    HANDLE StandardError;

    CURDIR CurrentDirectory;
    UNICODE_STRING DllPath;
    UNICODE_STRING ImagePathName;
    UNICODE_STRING CommandLine;
    PVOID Environment;

    ULONG StartingX;
    ULONG StartingY;
    ULONG CountX;
    ULONG CountY;
    ULONG CountCharsX;
    ULONG CountCharsY;
    ULONG FillAttribute;

    ULONG WindowFlags;
    ULONG ShowWindowFlags;
    UNICODE_STRING WindowTitle;
    UNICODE_STRING DesktopInfo;
    UNICODE_STRING ShellInfo;
    UNICODE_STRING RuntimeData;
    RTL_DRIVE_LETTER_CURDIR CurrentDirectories[RTL_MAX_DRIVE_LETTERS];

    ULONG EnvironmentSize;
    ULONG EnvironmentVersion;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

#define MAX_SYSCALL (64 * 1024)
static const char *g_syscall_names[MAX_SYSCALL];

// stole this lovely source code from the rreat library.
static void enum_syscalls()
{
    // no boundary checking at all, I assume ntdll is not malicious..
    // besides that, we are in our own process, _should_ be fine..
    unsigned char *image = (unsigned char *) W::GetModuleHandle("ntdll");
    W::IMAGE_DOS_HEADER *dos_header = (W::IMAGE_DOS_HEADER *) image;
    W::IMAGE_NT_HEADERS *nt_headers = (W::IMAGE_NT_HEADERS *)(image +
        dos_header->e_lfanew);
    W::IMAGE_DATA_DIRECTORY *data_directory = &nt_headers->
        OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    W::IMAGE_EXPORT_DIRECTORY *export_directory =
        (W::IMAGE_EXPORT_DIRECTORY *)(image + data_directory->VirtualAddress);
    unsigned long *address_of_names = (unsigned long *)(image +
        export_directory->AddressOfNames);
    unsigned long *address_of_functions = (unsigned long *)(image +
        export_directory->AddressOfFunctions);
    unsigned short *address_of_name_ordinals = (unsigned short *)(image +
        export_directory->AddressOfNameOrdinals);
    unsigned long number_of_names = MIN(export_directory->NumberOfFunctions,
        export_directory->NumberOfNames);
    for (unsigned long i = 0; i < number_of_names; i++) {
        const char *name = (const char *)(image + address_of_names[i]);
        unsigned char *addr = image + address_of_functions[
            address_of_name_ordinals[i]];
        if(!memcmp(name, "Zw", 2) || !memcmp(name, "Nt", 2)) {
            // does the signature match?
            // either:   mov eax, syscall_number ; mov ecx, some_value
            // or:       mov eax, syscall_number ; xor ecx, ecx
            if(*addr == 0xb8 && (addr[5] == 0xb9 || addr[5] == 0x33)) {
                unsigned long syscall_number = *(unsigned long *)(addr + 1);
                if(syscall_number < MAX_SYSCALL) {
                    g_syscall_names[syscall_number] = name;
                }
            }
        }
    }
}

void syscall_entry(THREADID thread_id, CONTEXT *ctx, SYSCALL_STANDARD std,
    void *v)
{
    unsigned long syscall_number = PIN_GetSyscallNumber(ctx, std);
    if(syscall_number < MAX_SYSCALL) {
        const char *name = g_syscall_names[syscall_number];
        printf("%d %s\n", thread_id, name);

        if(name != NULL && !strcmp(name, "ZwCreateUserProcess")) {
            RTL_USER_PROCESS_PARAMETERS *process_parameters =
                (RTL_USER_PROCESS_PARAMETERS *) PIN_GetSyscallArgument(ctx,
                    std, 8);

            printf("image_name: %S\ncommand_line: %S\n",
                process_parameters->ImagePathName.Buffer,
                process_parameters->CommandLine.Buffer);
        }
    }
    else {
        printf("dafuq?\n");
    }
}

void syscall_exit(THREADID thread_id, CONTEXT *ctx, SYSCALL_STANDARD std,
    void *v)
{
    unsigned long return_value = PIN_GetSyscallReturn(ctx, std);
}

int main(int argc, char *argv[])
{
    if(PIN_Init(argc, argv)) {
        printf("Usage: %s <binary> [arguments]\n");
        return 0;
    }

    enum_syscalls();

    PIN_AddSyscallEntryFunction(&syscall_entry, 0);
    PIN_AddSyscallExitFunction(&syscall_exit, 0);

    PIN_StartProgram();
    return 0;
}
