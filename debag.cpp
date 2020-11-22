#include <cstdio>
#include <ctime>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/personality.h>
#include <sys/user.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <capstone/capstone.h>
#include <libdwarf/dwarf.h>
#include <libdwarf/libdwarf.h>

#include <libs/imgui/imgui.h>
#include <libs/imgui/imgui_impl_glfw.h>
#include <libs/imgui/imgui_impl_opengl3.h>

#include <debag.h>
#include <flow.cpp>

static void
ImGuiStartFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

static void
ImGuiEndFrame()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static bool
CharInString(char *String, char C)
{
    while(String && String[0])
    {
        if(String[0] && String[0] == C)
        {
            return true;
        }
        String++;
    }
    
    return false;
}

static u32
StringCountChar(char *String, char C)
{
    u32 Result = 0;
    while(String && *String)
    {
        if(String[0] == C)
        {
            Result += 1;
        }
        String++;
    }
    
    return Result;
}

static void
StringCopy(char *Dest, char *Src)
{
    while(Dest && Src && Src[0])
    {
        Dest[0] = Src[0];
        
        Dest++;
        Src++;
    }
    Dest[0] = '\0';
}

static bool
StringsMatch(char *Str0, char *Str1)
{
    bool Result = true;
    
    while(Str0 && Str0[0] && Str1 && Str1[0])
    {
        if(Str0[0] != Str1[0])
        {
            Result = false;
            break;
        }
        Str0++;
        Str1++;
    }
    
    return Result;
}

static u64
HexStringToInt(char *String)
{
    u64 Result = 0;
    
    while(String[0] && String[0] != 'x')
    {
        String++;
    }
    
    String++;
    
    while(String[0])
    {
        Result *= 16;
        if(String[0] >= '0' && String[0] <= '9')
        {
            Result += String[0] - '0';
        }
        else
        {
            char C = TO_LOWERCASE(String[0]);
            assert(C >= 'a' && C <= 'f');
            
            Result += (C - 'a') + 10;
        }
        
        String++;
    }
    
    return Result;
}

static user_regs_struct
PeekRegisters(i32 DebugeePID)
{
    user_regs_struct Result = {};
    
    ptrace(PTRACE_GETREGS, DebugeePID, 0x0, &Result);
    
    return Result;
}

static void
SetRegisters(user_regs_struct Regs, i32 DebugeePID)
{
    ptrace(PTRACE_SETREGS, DebugeePID, 0x0, &Regs);
}

static u64
GetRegisterByABINumber(u32 Number)
{
    switch(Number)
    {
        case 0:
        return Regs.rax;
        case 1:
        return Regs.rdx;
        case 2:
        return Regs.rcx;
        case 3:
        return Regs.rbx;
        case 4:
        return Regs.rsi;
        case 5:
        return Regs.rdi;
        case 6:
        return Regs.rbp;
        case 7:
        return Regs.rsp;
        case 8:
        return Regs.r8;
        case 9:
        return Regs.r9;
        case 10:
        return Regs.r10;
        case 11:
        return Regs.r11;
        case 12:
        return Regs.r12;
        case 13:
        return Regs.r13;
        case 14:
        return Regs.r14;
        case 15:
        return Regs.r15;
        default:
        {
            assert(false);
        }break;
    }
}

static di_src_line *
LineTableFindByAddress(size_t Address)
{
    for(u32 I = 0; I < DWLineEntriesCount; I++)
    {
        if(DWLineTable[I].Address == Address)
        {
            return &DWLineTable[I];
        }
        else if(I + 1 < DWLineEntriesCount &&
                (DWLineTable[I].Address < Address) &&
                (DWLineTable[I + 1].Address > Address))
        {
            return &DWLineTable[I];
        }
    }
    
    return 0x0;
}

static di_src_line *
LineTableFindByLineNum(u32 LineNum)
{
    for(u32 I = 0; I < DWLineEntriesCount; I++)
    {
        if(DWLineTable[I].LineNum == LineNum)
        {
            return &DWLineTable[I];
        }
    }
    
    return 0x0;
}

static address_range
LineAddressRangeBetween(di_src_line *StartLine, di_src_line *EndLine)
{
    address_range Result = {};
    
    Result.Start = StartLine->Address;
    Result.End = EndLine->Address;
    
    return Result;
}

static dwarf_function *
FindFunctionConfiningAddress(size_t Address)
{
    dwarf_function *Result = 0x0;
    
    for(u32 I = 0; I < DWFunctionsCount; I++)
    {
        if(AddressBetween(Address, DWFunctions[I].LowPC, DWFunctions[I].HighPC))
        {
            Result = &DWFunctions[I];
            break;
        }
    }
    
    return Result;
}

static void
ImGuiShowRegisters(user_regs_struct Regs)
{
    ImGui::Columns(4, 0x0, true);
    
    ImGui::Text("r15: %lX", (u64)Regs.r15);
    ImGui::NextColumn();
    ImGui::Text("r14: %lX", (u64)Regs.r14);
    ImGui::NextColumn();
    ImGui::Text("r13: %lX", (u64)Regs.r13);
    ImGui::NextColumn();
    ImGui::Text("r12: %lX", (u64)Regs.r12);
    ImGui::NextColumn();
    ImGui::Text("rbp: %lX", (u64)Regs.rbp);
    ImGui::NextColumn();
    ImGui::Text("rbx: %lX", (u64)Regs.rbx);
    ImGui::NextColumn();
    ImGui::Text("r11: %lX", (u64)Regs.r11);
    ImGui::NextColumn();
    ImGui::Text("r10: %lX", (u64)Regs.r10);
    ImGui::NextColumn();
    ImGui::Text("r9: %lX", (u64)Regs.r9);
    ImGui::NextColumn();
    ImGui::Text("r8: %lX", (u64)Regs.r8);
    ImGui::NextColumn();
    ImGui::Text("rax: %lX", (u64)Regs.rax);
    ImGui::NextColumn();
    ImGui::Text("rcx: %lX", (u64)Regs.rcx);
    ImGui::NextColumn();
    ImGui::Text("rdx: %lX", (u64)Regs.rdx);
    ImGui::NextColumn();
    ImGui::Text("rsi: %lX", (u64)Regs.rsi);
    ImGui::NextColumn();
    ImGui::Text("rdi: %lX", (u64)Regs.rdi);
    ImGui::NextColumn();
    ImGui::Text("orig_rax: %lX", (u64)Regs.orig_rax);
    ImGui::NextColumn();
    ImGui::Text("rip: %lX", (u64)Regs.rip);
    ImGui::NextColumn();
    ImGui::Text("cs: %lX", (u64)Regs.cs);
    ImGui::NextColumn();
    ImGui::Text("eflags: %lX", (u64)Regs.eflags);
    ImGui::NextColumn();
    ImGui::Text("rsp: %lX", (u64)Regs.rsp);
    ImGui::NextColumn();
    ImGui::Text("ss: %lX", (u64)Regs.ss);
    ImGui::NextColumn();
    ImGui::Text("fs_base: %lX", (u64)Regs.fs_base);
    ImGui::NextColumn();
    ImGui::Text("gs_base: %lX", (u64)Regs.gs_base);
    ImGui::NextColumn();
    ImGui::Text("ds: %lX", (u64)Regs.ds);
    ImGui::NextColumn();
    ImGui::Text("es: %lX", (u64)Regs.es);
    ImGui::NextColumn();
    ImGui::Text("fs: %lX", (u64)Regs.fs);
    ImGui::NextColumn();
    ImGui::Text("gs: %lX", (u64)Regs.gs);
}

static size_t
PeekDebugeeMemory(size_t Address, i32 DebugeePID)
{
    size_t MachineWord = 0;
    
    MachineWord = ptrace(PTRACE_PEEKDATA, DebugeePID, Address, 0x0);
    
    return MachineWord;
}

static void
DisassembleAroundAddress(size_t Address, i32 DebugeePID)
{
    DisasmInstCount = 0;
    
    cs_insn *Instruction = {};
    size_t InstructionAddress = Address;
    for(int I = 0; I < MAX_DISASM_INSTRUCTIONS; I++)
    {
        size_t MachineWord = PeekDebugeeMemory(InstructionAddress, DebugeePID);
        
        breakpoint *BP = BreakpointFind(InstructionAddress, DebugeePID);
        if(BP)
        {
            MachineWord = (MachineWord & ~0xff) | BP->SavedOpCode;
        }
        
        int Count = cs_disasm(DisAsmHandle, (const u8 *)&MachineWord, sizeof(MachineWord),
                              InstructionAddress, 1, &Instruction);
        
        if(Count == 0) { break; }
        
        DisasmInst[I].Address = InstructionAddress;
        InstructionAddress += Instruction->size;
        
        assert(strlen(Instruction->mnemonic) < sizeof(DisasmInst[I].Mnemonic));
        assert(strlen(Instruction->op_str) < sizeof(DisasmInst[I].Operation));
        strcpy(DisasmInst[I].Mnemonic, Instruction->mnemonic);
        strcpy(DisasmInst[I].Operation, Instruction->op_str);
        DisasmInstCount++;
        
#if 0        
        if(Instruction->detail && Instruction->detail->groups_count > 0)
        {
            for(i32 GroupIndex = 0;
                GroupIndex < Instruction->detail->groups_count;
                GroupIndex++)
            {
                switch(Instruction->detail->groups[GroupIndex])
                {
                    case X86_GRP_INVALID:
                    {
                        printf("X86_GRP_INVALID, ");
                    }break;
                    case X86_GRP_JUMP:
                    {
                        printf("X86_GRP_JUMP, ");
                    }break;
                    case X86_GRP_CALL:
                    {
                        printf("X86_GRP_CALL, ");
                    }break;
                    case X86_GRP_RET:
                    {
                        printf("X86_GRP_RET, ");
                    }break;
                    case X86_GRP_INT:
                    {
                        printf("X86_GRP_INT, ");
                    }break;
                    case X86_GRP_IRET:
                    {
                        printf("X86_GRP_IRET, ");
                    }break;
                    case X86_GRP_PRIVILEGE:
                    {
                        printf("X86_GRP_PRIVILEGE, ");
                    }break;
                    case X86_GRP_BRANCH_RELATIVE:
                    {
                        printf("X86_GRP_BRANCH_RELATIVE, ");
                    }break;
                }
            }
            printf("%s: %s\n", DisasmInst[I].Mnemonic, DisasmInst[I].Operation);
        }
#endif
        
        cs_free(Instruction, 1);
    }
}

static char *
DumpFile(char *Path)
{
    FILE *FHandle = fopen(Path, "r");
    assert(FHandle);
    fseek(FHandle, 0, SEEK_END);
    u32 FileSize = ftell(FHandle);
    fseek(FHandle, 0, SEEK_SET);
    
    char *Result = (char *)malloc(FileSize + 1);
    fread(Result, FileSize, 1, FHandle);
    Result[FileSize] = '\0';
    
    return Result;
}

static src_file *
FindSourceFile(char *Path)
{
    src_file *Result = 0x0;
    
    for(u32 I = 0; I < SourceFilesCount; I++)
    {
        if(StringsMatch(Path, SourceFiles[I].Path))
        {
            Result = &SourceFiles[I];
            break;
        }
    }
    
    return Result;
}

static src_file *
PushSourceFile(char *Path)
{
    src_file *Result = 0x0;
    
    Result = &SourceFiles[SourceFilesCount++];
    
    Result->Path = strdup(Path);
    Result->Content = DumpFile(Path);
    Result->LineCount = StringCountChar(Result->Content, '\n');
    
    return Result;
}

static src_file *
GetSourceFile(char *Path)
{
    src_file *Result = 0x0;
    
    Result = FindSourceFile(Path);
    
    if(!Result)
    {
        Result = PushSourceFile(Path);
    }
    
    return Result;
}

static u32
SrcFileAssociatePath(char *Path)
{
    u32 Result = MAX_SOURCE_FILES + 1;
    
    assert(Path);
    for(u32 I = 0; I < SourceFilesCount; I++)
    {
        if(StringsMatch(Path, SourceFiles[I].Path))
        {
            Result = I;
            break;
        }
    }
    
    if(SourceFilesCount == 0 || Result == MAX_SOURCE_FILES + 1)
    {
        PushSourceFile(Path);
        Result = SourceFilesCount - 1;
    }
    
    return Result;
}

static address_range
AddressRangeCurrentAndNextLine()
{
    address_range Result = {};
    
    di_src_line *Current = LineTableFindByAddress(Regs.rip);
    for(u32 I = 0; I < DWLineEntriesCount; I++)
    {
        if(DWLineTable[I].Address == Current->Address)
        {
            for(;;I++)
            {
                if(DWLineEntriesCount == I + 1)
                {
                    dwarf_function *Func = FindFunctionConfiningAddress(Current->Address);
                    Result.Start = Current->Address;
                    Result.End = Func->HighPC;
                    goto end;
                }
                else
                {
                    di_src_line *Next = &DWLineTable[I];
                    if(Next->LineNum != Current->LineNum)
                    {
                        Result = LineAddressRangeBetween(Current, Next);
                        goto end;
                    }
                }
            }
        }
    }
    end:;
    
    
    return Result;
}

static size_t
FindEntryPointAddress()
{
    size_t Result = 0;
    
    for(u32 I = 0; I < DWFunctionsCount; I++)
    {
        if(StringsMatch(DWFunctions[I].Name, "main"))
        {
            Result = DWFunctions[I].LowPC;
            break;
        }
    }
    
    return Result;
}

static void
DWARFReadDIEsDebug(Dwarf_Debug Debug, Dwarf_Die DIE, i32 RecurLevel)
{
	Dwarf_Error Error_ = {};
    Dwarf_Error *Error = &Error_;
    Dwarf_Die CurrentDIE = DIE;
    
    Dwarf_Half Tag = 0;
    assert(dwarf_tag(CurrentDIE, &Tag, Error) == DW_DLV_OK);
    
    switch(Tag)
    {
        case DW_TAG_compile_unit:
        {
            //printf("CompUnit\n");
            Dwarf_Signed AttrCount = 0;
            Dwarf_Attribute *AttrList = {};
            DWARF_CALL(dwarf_attrlist(DIE, &AttrList, &AttrCount, Error));
            
            di_comp_unit *CompUnit = &DICompUnits[DICompUnitsCount++];
            for(u32 I = 0; I < AttrCount; I++)
            {
                Dwarf_Attribute Attribute = AttrList[I];
                Dwarf_Half AttrTag = 0;
                DWARF_CALL(dwarf_whatattr(Attribute, &AttrTag, Error));
                
                switch(AttrTag)
                {
                    case DW_AT_name:
                    {
                        char *Name = 0x0;
                        DWARF_CALL(dwarf_formstring(Attribute, &Name, Error));
                        StringCopy(CompUnit->Name, Name);
                    }break;
                    case DW_AT_low_pc:
                    {
                        Dwarf_Addr *WritePoint = (Dwarf_Addr *)&CompUnit->LowPC;
                        DWARF_CALL(dwarf_formaddr(Attribute, WritePoint, Error));
                    }break;
                    case DW_AT_high_pc:
                    {
                        Dwarf_Addr *WritePoint = (Dwarf_Addr *)&CompUnit->HighPC;
                        
                        Dwarf_Half Form = 0;
                        Dwarf_Form_Class FormType = {};
                        DWARF_CALL(dwarf_highpc_b(DIE, WritePoint, &Form, &FormType, 0x0));
                        if (FormType == DW_FORM_CLASS_CONSTANT) {
                            CompUnit->HighPC += CompUnit->LowPC;
                        }
                        
                    }break;
                    case DW_AT_ranges:
                    {
                        CompUnit->Flags |= DI_COMP_UNIT_HAS_RANGES;
                    }break;
                    default:
                    {
                        bool ignored = AttrTag == DW_AT_producer ||
                            AttrTag == DW_AT_comp_dir ||
                            AttrTag == DW_AT_stmt_list || AttrTag == DW_AT_language;
                        if(!ignored)
                        {
                            printf("CompUnit Unhandled Attribute: %d\n", AttrTag);
                        }
                    }break;
                }
            }
            
            Dwarf_Unsigned Version = 0;
            Dwarf_Small TableType = 0;
            Dwarf_Line_Context LineCtx = 0;
            DWARF_CALL(dwarf_srclines_b(DIE, &Version, &TableType, &LineCtx, Error));
            
            Dwarf_Line *LineBuffer = 0;
            Dwarf_Signed LineCount = 0;
            DWARF_CALL(dwarf_srclines_from_linecontext(LineCtx, &LineBuffer, &LineCount, Error));
            
            for (i32 I = 0; I < LineCount; ++I) {
                Dwarf_Addr LineAddr = 0;
                Dwarf_Unsigned FileNum = 0;
                Dwarf_Unsigned LineNum = 0;
                char *LineSrcFile = 0;
                
                DWARF_CALL(dwarf_lineno(LineBuffer[I], &LineNum, Error));
                DWARF_CALL(dwarf_line_srcfileno(LineBuffer[I], &FileNum, Error));
                if (FileNum) {
                    FileNum -= 1;
                }
                
                DWARF_CALL(dwarf_lineaddr(LineBuffer[I], &LineAddr, Error));
                DWARF_CALL(dwarf_linesrc(LineBuffer[I], &LineSrcFile, Error));
                
                di_src_line *LTEntry = &DWLineTable[DWLineEntriesCount++];
                LTEntry->Address = LineAddr;
                LTEntry->LineNum = LineNum;
                LTEntry->SrcFileIndex = SrcFileAssociatePath(LineSrcFile);
            }
        }break;
        case DW_TAG_subprogram:
        {
            //printf("Subprogram\n");
            Dwarf_Signed AttrCount = 0;
            Dwarf_Attribute *AttrList = {};
            DWARF_CALL(dwarf_attrlist(DIE, &AttrList, &AttrCount, Error));
            
            dwarf_function *Func = &DWFunctions[DWFunctionsCount++];
            for(u32 I = 0; I < AttrCount; I++)
            {
                Dwarf_Attribute Attribute = AttrList[I];
                Dwarf_Half AttrTag = 0;
                DWARF_CALL(dwarf_whatattr(Attribute, &AttrTag, Error));
                
                switch(AttrTag)
                {
                    case DW_AT_name:
                    {
                        char *Name = 0x0;
                        DWARF_CALL(dwarf_formstring(Attribute, &Name, Error));
                        
                        StringCopy(Func->Name, Name);
                    }break;
                    case DW_AT_low_pc:
                    {
                        Dwarf_Addr *WritePoint = (Dwarf_Addr *)&Func->LowPC;
                        DWARF_CALL(dwarf_formaddr(Attribute, WritePoint, Error));
                    }break;
                    case DW_AT_high_pc:
                    {
                        Dwarf_Addr *WritePoint = (Dwarf_Addr *)&Func->HighPC;
                        
                        Dwarf_Half Form = 0;
                        Dwarf_Form_Class FormType = {};
                        DWARF_CALL(dwarf_highpc_b(DIE, WritePoint, &Form, &FormType, 0x0));
                        if (FormType == DW_FORM_CLASS_CONSTANT) {
                            Func->HighPC += Func->LowPC;
                        }
                    }break;
                    case DW_AT_frame_base:
                    {
                        Dwarf_Loc_Head_c LocListHead = {};
                        Dwarf_Unsigned LocCount = 0;
                        DWARF_CALL(dwarf_get_loclist_c(Attribute, &LocListHead, &LocCount, Error));
                        
                        assert(LocCount == 1);
                        
                        for(u32 I = 0; I < LocCount; I++)
                        {
                            Dwarf_Small LLEOut = 0;
                            Dwarf_Addr LowPC = 0;
                            Dwarf_Addr HighPC = 0;
                            Dwarf_Unsigned LocListCountOut = 0;
                            Dwarf_Locdesc_c IDK = 0;
                            Dwarf_Small LocListSourceOut = 0;
                            Dwarf_Unsigned ExpressionOffsetOut = 0;
                            Dwarf_Unsigned LocDescOffsetOut = 0;
                            
                            DWARF_CALL(dwarf_get_locdesc_entry_c(LocListHead, I, &LLEOut, &LowPC, &HighPC, &LocListCountOut, &IDK, &LocListSourceOut, &ExpressionOffsetOut, 
                                                                 &LocDescOffsetOut, Error));
                            
                            Dwarf_Small AtomOut = 0;
                            Dwarf_Unsigned Operand1 = 0;
                            Dwarf_Unsigned Operand2 = 0;
                            Dwarf_Unsigned Operand3 = 0;
                            Dwarf_Unsigned OffsetBranch = 0;
                            DWARF_CALL(dwarf_get_location_op_value_c(IDK, I, &AtomOut, &Operand1, &Operand2, &Operand3, &OffsetBranch, Error));
                            
                            //printf("AtomOut = %d, Oper1 = %lld, Oper2 = %llu, Oper3 = %llu, OffsetBranch = %llu\n", AtomOut, Operand1, Operand2, Operand3, OffsetBranch);
                            
                            assert(AtomOut == DW_OP_call_frame_cfa);
                            Func->FrameBaseIsCFA = true;
                        }
                    }break;
                    default:
                    {
                        bool ignored = AttrTag == DW_AT_decl_file ||
                            AttrTag == DW_AT_decl_line ||
                            AttrTag == DW_AT_decl_column || AttrTag == DW_AT_prototyped ||
                            AttrTag == DW_AT_GNU_all_call_sites ||
                            AttrTag == DW_AT_external ||
                            AttrTag == DW_AT_GNU_all_tail_call_sites;
                        if(!ignored)
                        {
                            printf("Func Unhandled Attribute: %d\n", AttrTag);
                        }
                    }break;
                }
            }
        }break;
        case DW_TAG_variable:
        {
            //printf("Variable\n");
            Dwarf_Signed AttrCount = 0;
            Dwarf_Attribute *AttrList = {};
            DWARF_CALL(dwarf_attrlist(DIE, &AttrList, &AttrCount, Error));
            
            assert(DWFunctionsCount);
            dwarf_function *Func = &DWFunctions[DWFunctionsCount - 1];
            di_variable *Var = &Func->DIVariables[Func->DIVariablesCount++];
            for(u32 I = 0; I < AttrCount; I++)
            {
                Dwarf_Attribute Attribute = AttrList[I];
                Dwarf_Half AttrTag = 0;
                DWARF_CALL(dwarf_whatattr(Attribute, &AttrTag, Error));
                
                switch(AttrTag)
                {
                    case DW_AT_name:
                    {
                        char *Name = 0x0;
                        DWARF_CALL(dwarf_formstring(Attribute, &Name, Error));
                        
                        StringCopy(Var->Name, Name);
                    }break;
                    case DW_AT_type:
                    {
                        Dwarf_Off Offset = 0;
                        DWARF_CALL(dwarf_dietype_offset(CurrentDIE, &Offset, Error));
                        
                        Var->TypeOffset = Offset;
                    }break;
                    case DW_AT_location:
                    {
                        Dwarf_Loc_Head_c LocListHead = {};
                        Dwarf_Unsigned LocCount = 0;
                        DWARF_CALL(dwarf_get_loclist_c(Attribute, &LocListHead, &LocCount, Error));
                        
                        assert(LocCount == 1);
                        
                        for(u32 I = 0; I < LocCount; I++)
                        {
                            Dwarf_Small LLEOut = 0;
                            Dwarf_Addr LowPC = 0;
                            Dwarf_Addr HighPC = 0;
                            Dwarf_Unsigned LocListCountOut = 0;
                            Dwarf_Locdesc_c IDK = 0;
                            Dwarf_Small LocListSourceOut = 0;
                            Dwarf_Unsigned ExpressionOffsetOut = 0;
                            Dwarf_Unsigned LocDescOffsetOut = 0;
                            
                            DWARF_CALL(dwarf_get_locdesc_entry_c(LocListHead, I, &LLEOut, &LowPC, &HighPC, &LocListCountOut, &IDK, &LocListSourceOut, &ExpressionOffsetOut, 
                                                                 &LocDescOffsetOut, Error));
                            
                            Dwarf_Small AtomOut = 0;
                            Dwarf_Unsigned Operand1 = 0;
                            Dwarf_Unsigned Operand2 = 0;
                            Dwarf_Unsigned Operand3 = 0;
                            Dwarf_Unsigned OffsetBranch = 0;
                            DWARF_CALL(dwarf_get_location_op_value_c(IDK, I, &AtomOut, &Operand1, &Operand2, &Operand3, &OffsetBranch, Error));
                            
                            //printf("AtomOut = %d, Oper1 = %lld, Oper2 = %llu, Oper3 = %llu, OffsetBranch = %llu\n", AtomOut, Operand1, Operand2, Operand3, OffsetBranch);
                            
                            assert(AtomOut == DW_OP_fbreg);
                            Var->UsesFBReg = true;
                            Var->Offset = Operand1;
                        }
                    }break;
                }
            }
            
        }break;
        default:
        {
            printf("Unhandled case: %d\n", Tag);
        }break;
    }
    
    //ParseForLineTable(Debug, DIE);
    
    Dwarf_Die ChildDIE = 0;
    i32 Result = dwarf_child(CurrentDIE, &ChildDIE, Error);
    
    if(Result == DW_DLV_OK)
    { 
        DWARFReadDIEsDebug(Debug, ChildDIE, RecurLevel + 1);
        Dwarf_Die SiblingDIE = ChildDIE;
        while(Result == DW_DLV_OK)
        {
            CurrentDIE = SiblingDIE;
            Result = dwarf_siblingof(Debug, CurrentDIE, &SiblingDIE, Error);
            if(Result == DW_DLV_OK)
            {
                DWARFReadDIEsDebug(Debug, SiblingDIE, RecurLevel + 1);
            }
            else
            {
                break;
            }
        };
    }
    
    return;
}

static void
DWARFReadDebug()
{
    i32 Fd = open(Debuger.DebugeeProgramPath, O_RDONLY);
    assert(Fd != -1);
    
    Dwarf_Debug Debug = 0;
    Dwarf_Handler ErrorHandle = 0;
    Dwarf_Ptr ErrorArg = 0;
    Dwarf_Error *Error  = 0;
    
    assert(dwarf_init(Fd, DW_DLC_READ, ErrorHandle, ErrorArg, &Debug, Error) == DW_DLV_OK);
    
    di_frame_info *Frame = &DIFrameInfo;
    DWARF_CALL(dwarf_get_fde_list_eh(Debug, &Frame->CIEs, &Frame->CIECount, &Frame->FDEs, &Frame->FDECount, Error));
    
    Dwarf_Unsigned CUHeaderLength = 0;
    Dwarf_Half Version = 0;
    Dwarf_Unsigned AbbrevOffset = 0;
    Dwarf_Half AddressSize = 0;
    Dwarf_Unsigned NextCUHeader = 0;
    
    for(i32 CUCount = 0;;++CUCount) {
        // NOTE(mateusz): I don't know what it does
        i32 Result = dwarf_next_cu_header(Debug, &CUHeaderLength,
                                          &Version, &AbbrevOffset, &AddressSize,
                                          &NextCUHeader, Error);
        
        assert(Result != DW_DLV_ERROR);
        if(Result  == DW_DLV_NO_ENTRY) {
            break;
        }
        
        /* The CU will have a single sibling, a cu_die. */
        Dwarf_Die CurrentDIE = 0;
        Result = dwarf_siblingof(Debug, 0, &CurrentDIE, Error);
        assert(Result != DW_DLV_ERROR && Result != DW_DLV_NO_ENTRY);
        
        DWARFReadDIEsDebug(Debug, CurrentDIE, 0);
    }
    
    close(Fd);
    //assert(dwarf_finish(Debug, Error) == DW_DLV_OK);
}

static size_t
DWARFGetCFA(size_t PC)
{
    di_frame_info *Frame = &DIFrameInfo;
    for(u32 J = 0; J < Frame->FDECount; J++)
    {
        Dwarf_Error *Error  = 0;
        Dwarf_Addr FDELowPC = 0;
        Dwarf_Unsigned FDEFunctionLength = 0;
        DWARF_CALL(dwarf_get_fde_range(Frame->FDEs[J], &FDELowPC, &FDEFunctionLength,
                                       0x0, 0x0, 0x0, 0x0, 0x0, Error));
        
        if(AddressBetween(PC, FDELowPC, FDELowPC + FDEFunctionLength - 1))
        {
            Dwarf_Regtable3 Tab3 = {};
            Dwarf_Addr ActualPC = 0;
            DWARF_CALL(dwarf_get_fde_info_for_all_regs3(Frame->FDEs[J], PC, &Tab3, &ActualPC, Error));
            
            Dwarf_Small OffsetRel = Tab3.rt3_cfa_rule.dw_offset_relevant;
            Dwarf_Signed OffsetOut = Tab3.rt3_cfa_rule.dw_offset_or_block_len;
            Dwarf_Half RegnumOut = Tab3.rt3_cfa_rule.dw_regnum;
            
            assert(OffsetRel == 1);
            size_t RegVal = GetRegisterByABINumber(RegnumOut);
            
            //printf("RegVal = %lX, OffsetOut = %llX, RegVal + OffsetOut = %lX\n", RegVal, OffsetOut, (size_t)((ssize_t)RegVal + (ssize_t)OffsetOut));
            return RegVal + OffsetOut;
        }
    }
    
    assert(false);
    return PC;
}

static void
UpdateInfo()
{
    Regs = PeekRegisters(Debuger.DebugeePID);
    DisassembleAroundAddress(Regs.rip, Debuger.DebugeePID);
}

static void
DebugeeStart()
{
    i32 ProcessID = fork();
    
    // Child process
    if(ProcessID == 0)
    {
        personality(ADDR_NO_RANDOMIZE);
        ptrace(PTRACE_TRACEME, 0, 0x0, 0x0);
        execl(Debuger.DebugeeProgramPath, Debuger.DebugeeProgramPath, NULL);
    }
    else
    {
        Debuger.DebugeePID = ProcessID;
        Debuger.Flags &= ~DBG_FLAG_CHILD_PROCESS_EXITED;
        WaitForSignal(Debuger.DebugeePID);
    }
}

static void
DebugStart()
{
    glfwInit();
    GLFWwindow *Window = glfwCreateWindow(800, 600, "debag", NULL, NULL);
    glfwMakeContextCurrent(Window);
    glewInit();
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO(); (void)IO;
    ImGui::StyleColorsDark();
    
    ImGui_ImplGlfw_InitForOpenGL(Window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    
    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    
    char TextBuff[64] = {};
    char TextBuff2[64] = {};
    char TextBuff3[64] = {};
    strcpy(TextBuff3, Debuger.DebugeeProgramPath);
    
    if(Debuger.DebugeeProgramPath)
    {
        DebugeeStart();
    }
    
    Regs = PeekRegisters(Debuger.DebugeePID);
    
    ImGuiInputTextFlags ITFlags = 0;
    ITFlags |= ImGuiInputTextFlags_EnterReturnsTrue;
    
    assert(cs_open(CS_ARCH_X86, CS_MODE_64, &DisAsmHandle) == CS_ERR_OK);
    //cs_option(DisAsmHandle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT); 
    cs_option(DisAsmHandle, CS_OPT_DETAIL, CS_OPT_ON); 
    
    DWARFReadDebug();
    // NOTE(mateusz): For debug purpouses
    size_t EntryPointAddress = FindEntryPointAddress();
    assert(EntryPointAddress);
    
    breakpoint BP = BreakpointCreate(EntryPointAddress, Debuger.DebugeePID);
    BreakpointEnable(&BP);
    Breakpoints[BreakpointCount++] = BP;
    
    while(!glfwWindowShouldClose(Window))
    {
        glClear(GL_COLOR_BUFFER_BIT);
        
        ImGuiStartFrame();
        
        ImGui::Begin("Control window");
        
        ImGui::InputText("Program path", TextBuff3, 64, ITFlags);
        
        ImGui::InputText("", TextBuff, 64, ITFlags);
        ImGui::SameLine();
        
        if(ImGui::Button("Break"))
        {
            u64 Address;
            
            if(CharInString(TextBuff, 'x'))
            {
                Address = HexStringToInt(TextBuff);
            }
            else
            {
                Address = atol(TextBuff);
            }
            
            breakpoint BP = BreakpointCreate(Address, Debuger.DebugeePID);
            BreakpointEnable(&BP);
            Breakpoints[BreakpointCount++] = BP;
            
            UpdateInfo();
        }
        
        ImGui::InputText("tbr", TextBuff2, 64, ITFlags);
        ImGui::SameLine();
        
        if(ImGui::Button("BreakFunc"))
        {
            for(u32 I = 0; I < DWFunctionsCount; I++)
            {
                dwarf_function *Func = &DWFunctions[I];
                if(strcmp(TextBuff2, Func->Name) == 0)
                {
                    breakpoint BP = BreakpointCreate(Func->LowPC, Debuger.DebugeePID);
                    BreakpointEnable(&BP);
                    Breakpoints[BreakpointCount++] = BP;
                }
            }
            
            UpdateInfo();
        }
        
        if(ImGui::Button("Continue"))
        {
            ContinueProgram(Debuger.DebugeePID);
            UpdateInfo();
        }
        
        if(ImGui::Button("Single Step"))
        {
            StepInstruction(Debuger.DebugeePID);
            UpdateInfo();
        }
        
        if(ImGui::Button("Step"))
        {
            ToNextLine(Debuger.DebugeePID, true);
            UpdateInfo();
        }
        
        ImGui::SameLine();
        
        if(ImGui::Button("Next"))
        {
            ToNextLine(Debuger.DebugeePID, false);
            UpdateInfo();
        }
        
        if((Debuger.Flags & DBG_FLAG_CHILD_PROCESS_EXITED) && ImGui::Button("Restart process"))
        {
            DebugeeStart();
            
            // NOTE(mateusz): For debug purpouses
            size_t EntryPointAddress = FindEntryPointAddress();
            assert(EntryPointAddress);
            
            breakpoint BP = BreakpointCreate(EntryPointAddress, Debuger.DebugeePID);
            BreakpointEnable(&BP);
            Breakpoints[BreakpointCount++] = BP;
            
            DWARFReadDebug();
        }
        
        ImGui::End();
        
        ImGui::Begin("Program variables");
        
        if(ImGui::BeginTabBar("Vars", ImGuiTabBarFlags_None))
        {
            if(ImGui::BeginTabItem("x64 Registers"))
            {
                ImGuiShowRegisters(Regs);
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Locals"))
            {
                dwarf_function *Func = FindFunctionConfiningAddress(Regs.rip);
                if(Func && Func->FrameBaseIsCFA)
                {
                    size_t FBReg = DWARFGetCFA(Regs.rip);
                    for(u32 I = 0; I < Func->DIVariablesCount; I++)
                    {
                        di_variable *Var = &Func->DIVariables[I];
                        
                        if(Var->UsesFBReg)
                        {
                            // TODO(mateusz): Right now only ints
                            size_t VarAddress = FBReg + Var->Offset;
                            
                            size_t MachineWord = PeekDebugeeMemory(VarAddress, Debuger.DebugeePID);
                            i32 Value = (i32)MachineWord;
                            ImGui::Text("%s: %d", Var->Name, Value);
                        }
                    }
                }
                
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        
        ImGui::End();
        
        ImGui::Begin("Listings");
        
        if(ImGui::BeginTabBar("Source and Disassebmly", ImGuiTabBarFlags_None))
        {
            if(ImGui::BeginTabItem("Source code"))
            {
                di_src_line *Line = LineTableFindByAddress(Regs.rip);
                
                if(Line)
                {
                    src_file *Src = &SourceFiles[Line->SrcFileIndex];
                    
                    char *LinePtr = Src->Content;
                    char *Prev = 0x0;
                    for(u32 I = 0; I < Src->LineCount + 1; I++)
                    {
                        Prev = LinePtr;
                        LinePtr = strchr(LinePtr, '\n') + 1;
                        u32 LineLength = (u64)LinePtr - (u64)Prev;
                        
                        // NOTE(mateusz): Lines are indexed from 1
                        if(Line->LineNum == I + 1)
                        {
                            ImGui::TextColored(CurrentLineColor, "%.*s",
                                               LineLength, Prev);
                        }
                        else
                        {
                            di_src_line *DrawingLine = LineTableFindByLineNum(I + 1);
                            
                            if(DrawingLine && BreakpointFind(DrawingLine->Address, Debuger.DebugeePID))
                            {
                                ImGui::TextColored(BreakpointLineColor, "%.*s",
                                                   LineLength, Prev);
                            }
                            else
                            {
                                ImGui::Text("%.*s", LineLength, Prev);
                            }
                        }
                    }
                }
                
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Disassembly"))
            {
                for(u32 I = 0; I < DisasmInstCount; I++)
                {
                    disasm_inst *Inst = &DisasmInst[I];
                    
                    if(Inst->Address == Regs.rip)
                    {
                        ImGui::TextColored(CurrentLineColor,
                                           "0x%" PRIx64 ":\t%s\t\t%s\n",
                                           Inst->Address, Inst->Mnemonic, Inst->Operation);
                    }
                    else
                    {
                        ImGui::Text("0x%" PRIx64 ":\t%s\t\t%s\n",
                                    Inst->Address, Inst->Mnemonic, Inst->Operation);
                    }
                }
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        ImGui::End();
        
        ImGuiEndFrame();
        
        glfwPollEvents();
        glfwSwapBuffers(Window);
    }
}

int
main(i32 ArgCount, char **Args)
{
    if(ArgCount != 2)
    {
        return -1;
    }
    
    Debuger.DebugeeProgramPath = Args[1];
    DebugStart();
    
    return 0;
}