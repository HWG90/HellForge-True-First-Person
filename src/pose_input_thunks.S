/* The three assembly entries of the retail editor/pose helper (native/first_person/native_ads_input.S
   at 0.5.41-smooth.66), for the C translation of the helper (src/pose_helper.c). GNU syntax, Win64,
   built by zig cc for x86_64-windows-gnu. They edit no game code: the two thunks are values the
   helper puts in a Unit API dispatch slot, the third is its 32-bit compare-exchange.

   Why assembly: at entry R15 is the game's camera request (a non-volatile register of the look
   evaluator that calls the slot) and the caller's stack pointer S is the entry RSP + 8. C can read
   neither.

   Retail's thunks leave RCX / RDX (unit, node) where they are and call
       NADSE_Input90Call(unit, node, request, native_stack, caller)
       NADSE_Input00Call(unit, node, request, native_stack, caller)
   with R8 = R15, R9 = RSP+64 and the thunk's own return address at [RSP+32]. The C entries take the
   return address first (docs/translation/POSE-HELPER-TRANSLATION.md section 1), so the arguments move:
       tfp_pose_input90_from(caller, unit, node, request, native_stack)
       tfp_pose_position_getter_from(caller, unit, node, native_stack, native_request)
   The frame is retail's: 56 bytes (32 home + the fifth argument + alignment), so [RSP+56] is the
   return address and RSP+64 the caller's S. unit and node are uint32_t: the 32-bit moves are enough.
   The result (the game's own pointer) comes back in RAX untouched. */
.text
.p2align 4
.globl tfp_pose_input90_thunk
.def tfp_pose_input90_thunk; .scl 2; .type 32; .endef
.seh_proc tfp_pose_input90_thunk
tfp_pose_input90_thunk:
 subq $56,%rsp
 .seh_stackalloc 56
 .seh_endprologue
 movl %edx,%r8d
 movl %ecx,%edx
 movq 56(%rsp),%rcx
 movq %r15,%r9
 leaq 64(%rsp),%rax
 movq %rax,32(%rsp)
 call tfp_pose_input90_from
 addq $56,%rsp
 ret
.seh_endproc

/* uint32_t tfp_pose_input_compare_exchange(uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed)
   Retail's NADSE_InputCompareExchange, unchanged: lock cmpxchg on the 32 bits at `at`, the value
   found to *observed, 1. Its language handler (tfp_pose_input_cas_exception in src/wire_win_pose.c,
   retail's NADSE_InputCASException) resumes at the recovery label, which answers 0, when exactly
   this instruction faults on exactly these four bytes. An explicit handler, as in retail: an
   optimiser can drop a C __try around an intrinsic atomic. */
.p2align 4
.globl tfp_pose_input_compare_exchange
.def tfp_pose_input_compare_exchange; .scl 2; .type 32; .endef
.seh_proc tfp_pose_input_compare_exchange
.seh_handler tfp_pose_input_cas_exception,@except
tfp_pose_input_compare_exchange:
 .seh_endprologue
 movl %edx,%eax
.globl tfp_pose_input_cas_fault
tfp_pose_input_cas_fault:
 lock cmpxchgl %r8d,(%rcx)
 movl %eax,(%r9)
 movl $1,%eax
 ret
.globl tfp_pose_input_cas_recovery
tfp_pose_input_cas_recovery:
 xorl %eax,%eax
 ret
.seh_endproc

.p2align 4
.globl tfp_pose_input00_thunk
.def tfp_pose_input00_thunk; .scl 2; .type 32; .endef
.seh_proc tfp_pose_input00_thunk
tfp_pose_input00_thunk:
 subq $56,%rsp
 .seh_stackalloc 56
 .seh_endprologue
 movl %edx,%r8d
 movl %ecx,%edx
 movq 56(%rsp),%rcx
 leaq 64(%rsp),%r9
 movq %r15,32(%rsp)
 call tfp_pose_position_getter_from
 addq $56,%rsp
 ret
.seh_endproc
