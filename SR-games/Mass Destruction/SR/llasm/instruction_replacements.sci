loc_51F84,7,;lea dx, loc_B897E|mov tmp1, loc_B897E|ins16 edx, edx, tmp1

loc_3F333,5,;mov ds:0x2f5e2, eax|store eax, md_smc_3f5e2, 4
loc_3F33F,5,;mov ds:0x2f5d7, eax|store eax, md_smc_3f5d7, 4
loc_3F5D5,6,;mov ah, [edi+0x12345678]|load tmp1, md_smc_3f5d7, 4|add tmpadr, edi, tmp1|load8z tmp2, tmpadr, 1|ins8hl eax, eax, tmp2
loc_3F5E0,6,;mov al, [eax+0x12345678]|load tmp1, md_smc_3f5e2, 4|add tmpadr, eax, tmp1|load8z tmp2, tmpadr, 1|ins8ll eax, eax, tmp2

loc_4123E,5,;mov ds:0x3154f, eax|store eax, md_smc_4154f, 4
loc_414E6,5,;mov ds:0x3153b, eax|store eax, md_smc_4153b, 4
loc_414EB,6,;mov ds:0x31532, esi|store esi, md_smc_41532, 4
loc_41512,6,;add ecx, ds:0x3153b|load tmp1, md_smc_4153b, 4|add tmp2, ecx, tmp1|cmovult tmp2, tmp1, tmp3, 1, 0|mov ecx, tmp2|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp3
loc_41530,6,;adc edx, 0x12345678|load tmp1, md_smc_41532, 4|and tmp2, eflags, 1|add tmp3, edx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov edx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_41539,6,;adc ecx, 0x12345678|load tmp1, md_smc_4153b, 4|and tmp2, eflags, 1|add tmp3, ecx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov ecx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_4154D,6,;mov al, [eax+0x12345678]|load tmp1, md_smc_4154f, 4|add tmpadr, eax, tmp1|load8z tmp2, tmpadr, 1|ins8ll eax, eax, tmp2

loc_407B0,5,;mov ds:0x30b57,eax|store eax, md_smc_40b57, 4
loc_407B5,5,;mov ds:0x30b73,eax|store eax, md_smc_40b73, 4
loc_407BA,5,;mov ds:0x30b92,eax|store eax, md_smc_40b92, 4
loc_407BF,5,;mov ds:0x30bb1,eax|store eax, md_smc_40bb1, 4
loc_407C4,5,;mov ds:0x30bd0,eax|store eax, md_smc_40bd0, 4
loc_40A6C,5,;mov ds:0x30b4b,eax|store eax, md_smc_40b4b, 4
loc_40A71,5,;mov ds:0x30b6b,eax|store eax, md_smc_40b6b, 4
loc_40A76,5,;mov ds:0x30b8a,eax|store eax, md_smc_40b8a, 4
loc_40A7B,5,;mov ds:0x30ba9,eax|store eax, md_smc_40ba9, 4
loc_40A80,5,;mov ds:0x30bc8,eax|store eax, md_smc_40bc8, 4
loc_40A85,6,;mov DWORD PTR ds:0x30b42,esi|store esi, md_smc_40b42, 4
loc_40A8B,6,;mov DWORD PTR ds:0x30b62,esi|store esi, md_smc_40b62, 4
loc_40A91,6,;mov DWORD PTR ds:0x30b81,esi|store esi, md_smc_40b81, 4
loc_40A97,6,;mov DWORD PTR ds:0x30ba0,esi|store esi, md_smc_40ba0, 4
loc_40A9D,6,;mov DWORD PTR ds:0x30bbf,esi|store esi, md_smc_40bbf, 4
loc_40AC4,6,;add ecx,DWORD PTR ds:0x30b4b|load tmp1, md_smc_40b4b, 4|add tmp2, ecx, tmp1|cmovult tmp2, tmp1, tmp3, 1, 0|mov ecx, tmp2|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp3
loc_40B40,6,;adc edx,0x12345678|load tmp1, md_smc_40b42, 4|and tmp2, eflags, 1|add tmp3, edx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov edx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_40B49,6,;adc ecx,0x12345678|load tmp1, md_smc_40b4b, 4|and tmp2, eflags, 1|add tmp3, ecx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov ecx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_40B55,6,;mov al,BYTE PTR [eax+0x12345678]|load tmp1, md_smc_40b57, 4|add tmpadr, eax, tmp1|load8z tmp2, tmpadr, 1|ins8ll eax, eax, tmp2
loc_40B60,6,;adc edx,0x12345678|load tmp1, md_smc_40b62, 4|and tmp2, eflags, 1|add tmp3, edx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov edx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_40B69,6,;adc ecx,0x12345678|load tmp1, md_smc_40b6b, 4|and tmp2, eflags, 1|add tmp3, ecx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov ecx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_40B71,6,;mov al,BYTE PTR [eax+0x12345678]|load tmp1, md_smc_40b73, 4|add tmpadr, eax, tmp1|load8z tmp2, tmpadr, 1|ins8ll eax, eax, tmp2
loc_40B7F,6,;adc edx,0x12345678|load tmp1, md_smc_40b81, 4|and tmp2, eflags, 1|add tmp3, edx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov edx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_40B88,6,;adc ecx,0x12345678|load tmp1, md_smc_40b8a, 4|and tmp2, eflags, 1|add tmp3, ecx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov ecx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_40B90,6,;mov al,BYTE PTR [eax+0x12345678]|load tmp1, md_smc_40b92, 4|add tmpadr, eax, tmp1|load8z tmp2, tmpadr, 1|ins8ll eax, eax, tmp2
loc_40B9E,6,;adc edx,0x12345678|load tmp1, md_smc_40ba0, 4|and tmp2, eflags, 1|add tmp3, edx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov edx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_40BA7,6,;adc ecx,0x12345678|load tmp1, md_smc_40ba9, 4|and tmp2, eflags, 1|add tmp3, ecx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov ecx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_40BAF,6,;mov al,BYTE PTR [eax+0x12345678]|load tmp1, md_smc_40bb1, 4|add tmpadr, eax, tmp1|load8z tmp2, tmpadr, 1|ins8ll eax, eax, tmp2
loc_40BBD,6,;adc edx,0x12345678|load tmp1, md_smc_40bbf, 4|and tmp2, eflags, 1|add tmp3, edx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov edx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_40BC6,6,;adc ecx,0x12345678|load tmp1, md_smc_40bc8, 4|and tmp2, eflags, 1|add tmp3, ecx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov ecx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_40BCE,6,;mov al,BYTE PTR [eax+0x12345678]|load tmp1, md_smc_40bd0, 4|add tmpadr, eax, tmp1|load8z tmp2, tmpadr, 1|ins8ll eax, eax, tmp2
loc_40CB2,5,;mov ds:0x31077,eax|store eax, md_smc_41077, 4
loc_40CB7,5,;mov ds:0x310b6,eax|store eax, md_smc_410b6, 4
loc_40CBC,5,;mov ds:0x310e8,eax|store eax, md_smc_410e8, 4
loc_40CC1,5,;mov ds:0x3111a,eax|store eax, md_smc_4111a, 4
loc_40CC6,5,;mov ds:0x3114c,eax|store eax, md_smc_4114c, 4
loc_40CD4,5,;mov ds:0x3107f,eax|store eax, md_smc_4107f, 4
loc_40CD9,5,;mov ds:0x310c0,eax|store eax, md_smc_410c0, 4
loc_40CDF,5,;mov ds:0x310f2,eax|store eax, md_smc_410f2, 4
loc_40CE5,5,;mov ds:0x31124,eax|store eax, md_smc_41124, 4
loc_40CEB,5,;mov ds:0x31154,eax|store eax, md_smc_41154, 4
loc_40F93,5,;mov ds:0x3106b,eax|store eax, md_smc_4106b, 4
loc_40F98,5,;mov ds:0x310ae,eax|store eax, md_smc_410ae, 4
loc_40F9D,5,;mov ds:0x310e0,eax|store eax, md_smc_410e0, 4
loc_40FA2,5,;mov ds:0x31112,eax|store eax, md_smc_41112, 4
loc_40FA7,5,;mov ds:0x31144,eax|store eax, md_smc_41144, 4
loc_40FAC,6,;mov DWORD PTR ds:0x31062,esi|store esi, md_smc_41062, 4
loc_40FB2,6,;mov DWORD PTR ds:0x310a5,esi|store esi, md_smc_410a5, 4
loc_40FB8,6,;mov DWORD PTR ds:0x310d7,esi|store esi, md_smc_410d7, 4
loc_40FBE,6,;mov DWORD PTR ds:0x31109,esi|store esi, md_smc_41109, 4
loc_40FC4,6,;mov DWORD PTR ds:0x3113b,esi|store esi, md_smc_4113b, 4
loc_40FEB,6,;add ecx,DWORD PTR ds:0x3106b|load tmp1, md_smc_4106b, 4|add tmp2, ecx, tmp1|cmovult tmp2, tmp1, tmp3, 1, 0|mov ecx, tmp2|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp3
loc_41060,6,;adc edx,0x12345678|load tmp1, md_smc_41062, 4|and tmp2, eflags, 1|add tmp3, edx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov edx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_41069,6,;adc ecx,0x12345678|load tmp1, md_smc_4106b, 4|and tmp2, eflags, 1|add tmp3, ecx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov ecx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_41075,6,;mov al,BYTE PTR [eax+0x12345678]|load tmp1, md_smc_41077, 4|add tmpadr, eax, tmp1|load8z tmp2, tmpadr, 1|ins8ll eax, eax, tmp2
loc_4107D,6,;mov ah,BYTE PTR [edi+0x12345678]|load tmp1, md_smc_4107f, 4|add tmpadr, edi, tmp1|load8z tmp2, tmpadr, 1|ins8hl eax, eax, tmp2
loc_410A3,6,;adc edx,0x12345678|load tmp1, md_smc_410a5, 4|and tmp2, eflags, 1|add tmp3, edx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov edx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_410AC,6,;adc ecx,0x12345678|load tmp1, md_smc_410ae, 4|and tmp2, eflags, 1|add tmp3, ecx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov ecx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_410B4,6,;mov al,BYTE PTR [eax+0x12345678]|load tmp1, md_smc_410b6, 4|add tmpadr, eax, tmp1|load8z tmp2, tmpadr, 1|ins8ll eax, eax, tmp2
loc_410BE,6,;mov ah,BYTE PTR [edi+0x12345678]|load tmp1, md_smc_410c0, 4|add tmpadr, edi, tmp1|load8z tmp2, tmpadr, 1|ins8hl eax, eax, tmp2
loc_410D5,6,;adc edx,0x12345678|load tmp1, md_smc_410d7, 4|and tmp2, eflags, 1|add tmp3, edx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov edx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_410DE,6,;adc ecx,0x12345678|load tmp1, md_smc_410e0, 4|and tmp2, eflags, 1|add tmp3, ecx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov ecx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_410E6,6,;mov al,BYTE PTR [eax+0x12345678]|load tmp1, md_smc_410e8, 4|add tmpadr, eax, tmp1|load8z tmp2, tmpadr, 1|ins8ll eax, eax, tmp2
loc_410F0,6,;mov ah,BYTE PTR [edi+0x12345678]|load tmp1, md_smc_410f2, 4|add tmpadr, edi, tmp1|load8z tmp2, tmpadr, 1|ins8hl eax, eax, tmp2
loc_41107,6,;adc edx,0x12345678|load tmp1, md_smc_41109, 4|and tmp2, eflags, 1|add tmp3, edx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov edx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_41110,6,;adc ecx,0x12345678|load tmp1, md_smc_41112, 4|and tmp2, eflags, 1|add tmp3, ecx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov ecx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_41118,6,;mov al,BYTE PTR [eax+0x12345678]|load tmp1, md_smc_4111a, 4|add tmpadr, eax, tmp1|load8z tmp2, tmpadr, 1|ins8ll eax, eax, tmp2
loc_41122,6,;mov ah,BYTE PTR [edi+0x12345678]|load tmp1, md_smc_41124, 4|add tmpadr, edi, tmp1|load8z tmp2, tmpadr, 1|ins8hl eax, eax, tmp2
loc_41139,6,;adc edx,0x12345678|load tmp1, md_smc_4113b, 4|and tmp2, eflags, 1|add tmp3, edx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov edx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_41142,6,;adc ecx,0x12345678|load tmp1, md_smc_41144, 4|and tmp2, eflags, 1|add tmp3, ecx, tmp1|cmovult tmp3, tmp1, tmp4, 1, 0|add tmp5, tmp3, tmp2|cmovult tmp5, tmp2, tmp6, 1, 0|or tmp4, tmp4, tmp6|mov ecx, tmp5|and eflags, eflags, 0xfffffffe|or eflags, eflags, tmp4
loc_4114A,6,;mov al,BYTE PTR [eax+0x12345678]|load tmp1, md_smc_4114c, 4|add tmpadr, eax, tmp1|load8z tmp2, tmpadr, 1|ins8ll eax, eax, tmp2
loc_41152,6,;mov ah,BYTE PTR [edi+0x12345678]|load tmp1, md_smc_41154, 4|add tmpadr, edi, tmp1|load8z tmp2, tmpadr, 1|ins8hl eax, eax, tmp2
loc_41D2D,5,;mov ds:0x31f54,eax|store eax, md_smc_41f54, 4
loc_41F52,6,;mov bh,BYTE PTR [edi+0x12345678]|load tmp1, md_smc_41f54, 4|add tmpadr, edi, tmp1|load8z tmp2, tmpadr, 1|ins8hl ebx, ebx, tmp2

loc_60C92,3,;rol dl, 0x2|and tmp1, edx, 0xff|shl tmp2, tmp1, 2|lshr tmp3, tmp1, 6|or tmp2, tmp2, tmp3|and tmp2, tmp2, 0xff|ins8ll edx, edx, tmp2
loc_63025,3,;ror ax, cl|and tmp1, ecx, 0x0f|and tmp2, eax, 0xffff|lshr tmp3, tmp2, tmp1|sub tmp4, 16, tmp1|and tmp4, tmp4, 0x0f|shl tmp5, tmp2, tmp4|or tmp3, tmp3, tmp5|ins16 eax, eax, tmp3

loc_74759,3,;lsl eax, eax|load eax, md_ds_limit, 4

loc_74D28,8,;o16 smsw [loc_BF6F2]|mov tmp1, 0x0013|store16 tmp1, loc_BF6F2, 2
loc_74D3B,4,;sidt [esp]|mov tmp1, 0x07ff|store16 tmp1, esp, 4|load tmp2, md_idt_base, 4|add tmpadr, esp, 2|store tmp2, tmpadr, 2

loc_74E42,3,;mov ecx, cr0|load ecx, md_cr0, 4
loc_74E4B,3,;mov cr0, ecx|store ecx, md_cr0, 4
loc_74F10,3,;mov ecx, cr0|load ecx, md_cr0, 4
loc_74F20,3,;mov cr0, ecx|store ecx, md_cr0, 4
loc_7C3C2,3,;mov eax, cr0|load eax, md_cr0, 4
loc_7C3C9,3,;mov cr0, eax|store eax, md_cr0, 4
loc_7C429,3,;mov eax, cr0|load eax, md_cr0, 4
loc_7C42E,3,;mov cr0, eax|store eax, md_cr0, 4

loc_7D678,2,;sub edi, edi|load16z edi, loc_110049, 1|shl edi, edi, 4|load tmp1, md_lowmem_bias, 4|add edi, edi, tmp1
loc_7D7D5,2,;sub edi, edi|load16z edi, loc_110049, 1|shl edi, edi, 4|load tmp1, md_lowmem_bias, 4|add edi, edi, tmp1
loc_7D87C,2,;sub edi, edi|load16z edi, loc_110049, 1|shl edi, edi, 4|load tmp1, md_lowmem_bias, 4|add edi, edi, tmp1
loc_7D944,5,;mov esi, 0x1a|load16z esi, loc_110049, 1|shl esi, esi, 4|load tmp1, md_lowmem_bias, 4|add esi, esi, tmp1|add esi, esi, 0x1a
loc_7D9CC,5,;mov esi, 0x1a|load16z esi, loc_110049, 1|shl esi, esi, 4|load tmp1, md_lowmem_bias, 4|add esi, esi, tmp1|add esi, esi, 0x1a
loc_7DA5C,2,;sub edi, edi|load16z edi, loc_110049, 1|shl edi, edi, 4|load tmp1, md_lowmem_bias, 4|add edi, edi, tmp1
loc_7DA5E,5,;mov esi, 0x1a|load16z esi, loc_110049, 1|shl esi, esi, 4|load tmp1, md_lowmem_bias, 4|add esi, esi, tmp1|add esi, esi, 0x1a
loc_7DA97,5,;mov esi, 0x1a|load16z esi, loc_110049, 1|shl esi, esi, 4|load tmp1, md_lowmem_bias, 4|add esi, esi, tmp1|add esi, esi, 0x1a

loc_74928,11,;lea esi,[esi+esi*2]; lea eax,cs:[esi+loc_749CE]; push eax|store esi, md_int86_no, 4|PUSH md_int86_thunk

loc_13FD2,5,;mov eax, [Game_ScreenWindow]|load eax, Game_ScreenWindow, 4
loc_141B5,5,;mov edi, [Game_ScreenWindow]|load edi, Game_ScreenWindow, 4|tcall loc_141BA|endp
loc_144BB,5,;mov eax, [Game_ScreenWindow]|load eax, Game_ScreenWindow, 4
loc_145C1,5,;mov edi, [Game_ScreenWindow]|load edi, Game_ScreenWindow, 4|tcall loc_145C6|endp
loc_1480C,5,;mov eax, [Game_ScreenWindow]|load eax, Game_ScreenWindow, 4
loc_153B2,5,;mov edx, [Game_ScreenWindow]|load edx, Game_ScreenWindow, 4
loc_154A6,5,;mov eax, [Game_ScreenWindow]|load eax, Game_ScreenWindow, 4
loc_15525,5,;mov edx, [Game_ScreenWindow]|load edx, Game_ScreenWindow, 4
loc_155FC,5,;mov ebx, [Game_ScreenWindow]|load ebx, Game_ScreenWindow, 4
loc_15675,5,;mov eax, [Game_ScreenWindow]|load eax, Game_ScreenWindow, 4
loc_156A5,5,;mov edx, [Game_ScreenWindow]|load edx, Game_ScreenWindow, 4
loc_1575F,5,;mov ebx, [Game_ScreenWindow]|load ebx, Game_ScreenWindow, 4
loc_157CF,5,;mov eax, [Game_ScreenWindow]|load eax, Game_ScreenWindow, 4
loc_15830,5,;mov ebx, [Game_ScreenWindow]|load ebx, Game_ScreenWindow, 4
loc_158D4,5,;mov ebx, [Game_ScreenWindow]|load ebx, Game_ScreenWindow, 4
loc_20003,5,;mov esi, [Game_ScreenWindow]|load esi, Game_ScreenWindow, 4
loc_34DB6,5,;mov edx, [Game_ScreenWindow]|load edx, Game_ScreenWindow, 4
loc_34F04,5,;mov edx, [Game_ScreenWindow]|load edx, Game_ScreenWindow, 4
loc_35041,5,;mov edx, [Game_ScreenWindow]|load edx, Game_ScreenWindow, 4
loc_3699E,5,;mov edi, [Game_ScreenWindow]|load edi, Game_ScreenWindow, 4
loc_37E07,5,;mov edi, [Game_ScreenWindow]|load edi, Game_ScreenWindow, 4
loc_386E0,5,;mov edi, [Game_ScreenWindow]|load edi, Game_ScreenWindow, 4
loc_3AA81,5,;mov edi, [Game_ScreenWindow]|load edi, Game_ScreenWindow, 4
loc_3AA96,5,;mov edi, [Game_ScreenWindow]|load edi, Game_ScreenWindow, 4
loc_566BE,5,;mov eax, [Game_ScreenWindow]|load eax, Game_ScreenWindow, 4
loc_56703,5,;mov eax, [Game_ScreenWindow]|load eax, Game_ScreenWindow, 4
loc_57357,5,;mov eax, [Game_ScreenWindow]|load eax, Game_ScreenWindow, 4
loc_573B4,5,;mov edx, [Game_ScreenWindow]|load edx, Game_ScreenWindow, 4
loc_5747A,5,;mov eax, [Game_ScreenWindow]|load eax, Game_ScreenWindow, 4
loc_58A37,5,;mov eax, [Game_ScreenWindow]|load eax, Game_ScreenWindow, 4
loc_5AA97,5,;mov ebx, [Game_ScreenWindow]|;add ebx, 0xd200|load ebx, Game_ScreenWindow, 4|add ebx, ebx, 0xd200
loc_5B59E,5,;mov edi, [Game_ScreenWindow]|;add edi, 0xd200|load edi, Game_ScreenWindow, 4|add edi, edi, 0xd200
loc_5C7A1,5,;mov edi, [Game_ScreenWindow]|load edi, Game_ScreenWindow, 4
loc_5C953,5,;mov edx, [Game_ScreenWindow]|load edx, Game_ScreenWindow, 4
loc_5CA66,5,;mov edi, [Game_ScreenWindow]|load edi, Game_ScreenWindow, 4
loc_62384,5,;mov edi, [Game_ScreenWindow]|load edi, Game_ScreenWindow, 4
loc_625D6,5,;mov edi, [Game_ScreenWindow]|load edi, Game_ScreenWindow, 4|tcall loc_625DB|endp

loc_3DEF2,3,;shl eax, 0x4|shl tmp1, eax, 4|load tmp2, md_lowmem_bias, 4|add tmp1, tmp1, tmp2|cmovz eax, eax, 0, tmp1
loc_543FD,3,;shl eax, 0x4|shl tmp1, eax, 4|load tmp2, md_lowmem_bias, 4|add tmp1, tmp1, tmp2|cmovz eax, eax, 0, tmp1
