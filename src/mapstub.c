/*
 * QuickDllInject - System Informer Plugin
 *
 * Position-independent loader stubs (one per architecture). The stub runs
 * TLS callbacks and DllMain inside the target and optionally registers
 * the exception directory for 64-bit unwinding.
 *
 * Authors:
 *     Textic
 *
 */

#include "QuickDllInject.h"

#if defined(_M_X64)

// Loader stub (x64, params in RCX):
//   sub rsp,0x38; ... RtlAddFunctionTable(table,count,base);
//   ... TlsCallbacks[i](base, DLL_PROCESS_ATTACH, NULL);
//   ... return DllMain(base, DLL_PROCESS_ATTACH, NULL);
static UCHAR QuickMapStubX64[] =
{
    0x48, 0x83, 0xEC, 0x38,             // sub rsp,0x38
    0x48, 0x89, 0x4C, 0x24, 0x20,       // mov [rsp+0x20],rcx
    0x48, 0x8B, 0x01,                   // mov rax,[rcx]
    0x48, 0x85, 0xC0,                   // test rax,rax
    0x74, 0x18,                         // jz no_eh
    0x48, 0x8B, 0x49, 0x08,             // mov rcx,[rcx+8]
    0x48, 0x8B, 0x54, 0x24, 0x20,       // mov rdx,[rsp+0x20]
    0x48, 0x8B, 0x52, 0x10,             // mov rdx,[rdx+0x10]
    0x4C, 0x8B, 0x44, 0x24, 0x20,       // mov r8,[rsp+0x20]
    0x4C, 0x8B, 0x40, 0x30,             // mov r8,[r8+0x30]
    0xFF, 0xD0,                         // call rax
    // no_eh:
    0x48, 0x8B, 0x4C, 0x24, 0x20,       // mov rcx,[rsp+0x20]
    0x48, 0x8B, 0x41, 0x18,             // mov rax,[rcx+0x18]
    0x48, 0x85, 0xC0,                   // test rax,rax
    0x74, 0x37,                         // jz no_tls
    0x48, 0x89, 0x44, 0x24, 0x28,       // mov [rsp+0x28],rax
    0x4C, 0x8B, 0x51, 0x20,             // mov r10,[rcx+0x20]
    0x4D, 0x31, 0xDB,                   // xor r11,r11
    // tls_loop:
    0x4D, 0x39, 0xDA,                   // cmp r11,r10
    0x73, 0x26,                         // jae tls_done
    0x48, 0x8B, 0x44, 0x24, 0x28,       // mov rax,[rsp+0x28]
    0x48, 0x8B, 0x04, 0xDB,             // mov rax,[rax+r11*8]
    0x48, 0x85, 0xC0,                   // test rax,rax
    0x74, 0x13,                         // jz tls_next
    0x48, 0x8B, 0x4C, 0x24, 0x20,       // mov rcx,[rsp+0x20]
    0x48, 0x8B, 0x49, 0x30,             // mov rcx,[rcx+0x30]
    0xBA, 0x01, 0x00, 0x00, 0x00,       // mov edx,1
    0x4D, 0x31, 0xC0,                   // xor r8,r8
    0xFF, 0xD0,                         // call rax
    // tls_next:
    0x49, 0xFF, 0xC3,                   // inc r11
    0xEB, 0xD5,                         // jmp tls_loop
    // tls_done: (no_tls:)
    0x48, 0x8B, 0x4C, 0x24, 0x20,       // mov rcx,[rsp+0x20]
    0x48, 0x8B, 0x41, 0x28,             // mov rax,[rcx+0x28]
    0x48, 0x8B, 0x49, 0x30,             // mov rcx,[rcx+0x30]
    0xBA, 0x01, 0x00, 0x00, 0x00,       // mov edx,1
    0x4D, 0x31, 0xC0,                   // xor r8,r8
    0xFF, 0xD0,                         // call rax
    0x48, 0x83, 0xC4, 0x38,             // add rsp,0x38
    0xC3                                // ret
};

#elif defined(_M_IX86)

// Loader stub (x86 stdcall, params at [esp+4]).
static UCHAR QuickMapStubX86[] =
{
    0x53,                               // push ebx
    0x8B, 0x5C, 0x24, 0x08,             // mov ebx,[esp+8]
    0x8B, 0x0B,                         // mov ecx,[ebx]
    0x85, 0xC9,                         // test ecx,ecx
    0x74, 0x0C,                         // jz no_eh
    0xFF, 0x73, 0x30,                   // push [ebx+0x30]
    0xFF, 0x73, 0x10,                   // push [ebx+0x10]
    0xFF, 0x73, 0x08,                   // push [ebx+8]
    0xFF, 0xD1,                         // call ecx
    // no_eh:
    0x8B, 0x4B, 0x18,                   // mov ecx,[ebx+0x18]
    0x85, 0xC9,                         // test ecx,ecx
    0x74, 0x1E,                         // jz no_tls
    0x56,                               // push esi
    0x33, 0xF6,                         // xor esi,esi
    // tls_loop:
    0x3B, 0xB3, 0x20, 0x00, 0x00, 0x00, // cmp esi,[ebx+0x20]
    0x73, 0x0E,                         // jae tls_done
    0x8B, 0x04, 0xB1,                   // mov eax,[ecx+esi*4]
    0x85, 0xC0,                         // test eax,eax
    0x74, 0x09,                         // jz tls_next
    0x6A, 0x00,                         // push 0
    0x6A, 0x01,                         // push 1
    0xFF, 0x73, 0x30,                   // push [ebx+0x30]
    0xFF, 0xD0,                         // call eax
    // tls_next:
    0x46,                               // inc esi
    0xEB, 0xE5,                         // jmp tls_loop
    // tls_done:
    0x5E,                               // pop esi
    // no_tls:
    0x6A, 0x00,                         // push 0
    0x6A, 0x01,                         // push 1
    0xFF, 0x73, 0x30,                   // push [ebx+0x30]
    0x8B, 0x43, 0x28,                   // mov eax,[ebx+0x28]
    0xFF, 0xD0,                         // call eax
    0x5B,                               // pop ebx
    0xC2, 0x04, 0x00                    // ret 4
};

#elif defined(_M_ARM64)

// Loader stub (ARM64, params in x0). Little-endian words.
static ULONG QuickMapStubArm64[] =
{
    0xD100C3FF, // sub sp, sp, #48
    0xF90003E0, // str x0, [sp]
    0xF9400009, // ldr x9, [x0]
    0xB4000109, // cbz x9, no_eh
    0xF9400400, // ldr x0, [x0, #8]
    0xF94003E1, // ldr x1, [sp]
    0xB9401021, // ldr w1, [x1, #16]
    0xF94003E2, // ldr x2, [sp]
    0xF9401822, // ldr x2, [x2, #48]
    0xD63F0120, // blr x9
    0xF94003E0, // ldr x0, [sp]
    // no_eh:
    0xF9400C09, // ldr x9, [x0, #24]
    0xB4000289, // cbz x9, no_tls
    0xF9000829, // str x9, [sp, #16]
    0xD280000A, // mov x10, #0
    0xF900044A, // str x10, [sp, #8]
    0xF940100B, // ldr x11, [x0, #32]
    // tls_loop:
    0xF940044A, // ldr x10, [sp, #8]
    0xEB0B014F, // cmp x10, x11
    0x54A001AA, // b.hs tls_done
    0xF9400829, // ldr x9, [sp, #16]
    0xF86A6929, // ldr x9, [x9, x10, lsl #3]
    0xB40000C9, // cbz x9, tls_next
    0xF94003E0, // ldr x0, [sp]
    0xF9401820, // ldr x0, [x0, #48]
    0x52800021, // mov w1, #1
    0xD2800042, // mov x2, #0
    0xD63F0120, // blr x9
    // tls_next:
    0xF940044A, // ldr x10, [sp, #8]
    0x910004AA, // add x10, x10, #1
    0xF900044A, // str x10, [sp, #8]
    0x17FFFFF2, // b tls_loop
    // tls_done: (no_tls:)
    0xF94003E0, // ldr x0, [sp]
    0xF9401409, // ldr x9, [x0, #40]
    0xF9401820, // ldr x0, [x0, #48]
    0x52800021, // mov w1, #1
    0xD2800042, // mov x2, #0
    0xD63F0120, // blr x9
    0x9100C3FF, // add sp, sp, #48
    0xD65F03C0  // ret
};

#endif

PUCHAR QuickGetMapStub(
    _Out_ PSIZE_T StubSize
    )
{
#if defined(_M_X64)
    *StubSize = sizeof(QuickMapStubX64);
    return QuickMapStubX64;
#elif defined(_M_IX86)
    *StubSize = sizeof(QuickMapStubX86);
    return QuickMapStubX86;
#elif defined(_M_ARM64)
    *StubSize = sizeof(QuickMapStubArm64);
    return (PUCHAR)QuickMapStubArm64;
#else
    *StubSize = 0;
    return NULL;
#endif
}
