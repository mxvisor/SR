loc_2B750     ; push ebx / mov ebx,[edx] / mov eax,[eax] / sub eax,ebx / ret
loc_634CE
loc_695E4     ; mov edx,[edx+8] / jmp rel32
loc_69977
loc_6BE33
loc_72415
loc_7243F     ; stream callback: `mov ecx, loc_7243F` + call loc_73B5C
loc_790D8
loc_77A1F     ; these eight are entries of the dispatch table at loc_76B9C
loc_77ACE
loc_77B71
loc_77C00
loc_77CAD
loc_77D94
loc_77E69
loc_77F30
loc_419E2
loc_534CC
loc_53752
loc_54347
loc_5492C
loc_687CE     ; mov edx, loc_687CE
loc_68B7B
loc_69590     ; `mov ecx, loc_69590` -- the tank.ini read callback
loc_6B60B
loc_6DA08
loc_6DBB0
loc_6DC68
loc_6DF53
loc_6E22D
loc_717D0
loc_71AD3
loc_71E6A
loc_71E91
loc_74F45     ; fsave [eax] / fwait / ret   -- FPU context save
loc_74F4A     ; frstor [eax] / fwait / ret  -- FPU context restore
loc_79628
loc_79664
loc_10480     ; keyboard ISR (port 60h -> key-state table + ring buffer); installed via extended setvect INT 9
loc_1FB4C     ; qsort comparator: `mov ecx, loc_1FB4C` / ebx=6 / eax=loc_D8480, same shape as loc_2B750
loc_20094     ; `mov ebp, loc_20094` / `call ebp` (loc_3C74E)
loc_41209     ; `mov ebp, loc_41209` / `call ebp` (three sites near loc_3B3F0)
loc_41CE6     ; `mov ebp, loc_41CE6` / `call ebp` (loc_3AC32)
loc_51E9A     ; `mov ebp, loc_51E9A` / `call ebp` (loc_3B3E8, two sites)
loc_51EC4     ; `mov ebp, loc_51EC4` / `call ebp` (loc_3B4BD, two sites); called after "Select Commander"
loc_53F1D     ; the game's INT 8 handler: `mov edx, loc_53F1D` / eax=8 -> setvect (AX=2504h CL=8), installed as the main menu starts; drives the frame counter the menu animation waits on
