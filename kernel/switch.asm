BITS 32
global switch_context
global create_task_stack

; switch_context(uint32_t* old_esp_ptr, uint32_t new_esp)
switch_context:
    push ebp
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax
    mov eax, [esp+32]      ; old_esp_ptr
    mov [eax], esp         ; save ESP
    mov eax, [esp+36]      ; new_esp
    mov esp, eax
    pop eax
    pop ebx
    pop ecx
    pop edx
    pop esi
    pop edi
    pop ebp
    ret

; create_task_stack(uint32_t stack_top, uint32_t entry_point)
create_task_stack:
    mov eax, [esp+4]       ; stack_top
    mov ecx, [esp+8]       ; entry_point
    mov [eax-4], ecx       ; return address = entry point
    mov dword [eax-8], 0   ; ebp = 0
    mov dword [eax-12], 0  ; edi = 0
    mov dword [eax-16], 0  ; esi = 0
    mov dword [eax-20], 0  ; edx = 0
    mov dword [eax-24], 0  ; ecx = 0
    mov dword [eax-28], 0  ; ebx = 0
    mov dword [eax-32], 0  ; eax = 0
    lea eax, [eax-32]
    ret
