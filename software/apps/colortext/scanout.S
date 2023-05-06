.syntax unified
.cpu cortex-m0plus
.thumb

.macro decl_func_x name
.section .scratch_x.\name, "ax"
.global \name
.type \name,%function
.thumb_func
\name:
.endm

#define decl_func decl_func_x

decl_func fast_scanout
    push {r4, r5, r6, r7}
    mov r4, r8
    mov r5, r9
    push {r4, r5}
scanout_continue:
    ldmia r0!, {r4}
    uxth r5, r4
    adr r6, op_table
    lsrs r4, #14
    ldr r4, [r4, r6]
    bx r4

.align 4
op_table:
    .4byte op_stop
    .4byte op_solid
    .4byte op_solid_gray
    .4byte op_1bpp_pal

.type op_stop, %function
op_stop:
    pop {r4, r5}
    mov r8, r4
    mov r9, r5
    pop {r4, r5, r6, r7}
    bx lr

.type op_solid, %function
op_solid:
    lsls r5, #2
    adds r5, r2
    mov ip, r5
    ldmia r0!, {r4}
    ldr r6, =(tmds_table_4bpp)
    movs r7, #0xf
    mov ip, r4
    ands r4, r7
    movs r7, #0x44
    muls r4, r4, r7
    ldr r7, [r6, r4]
    // TODO finish
    b op_stop

.type op_solid_gray, %function
op_solid_gray:
    lsls r5, #2
    adds r5, r2
    ldmia r0!, {r4}
    movs r7, #0x44
    muls r4, r4, r7
    ldr r6, =(tmds_table_4bpp)
    ldr r4, [r6, r4]
1:
    str r4, [r2, r3]
    adds r6, r2, r3
    str r4, [r6, r3]
    stmia r2!, {r4}
2:
    cmp r2, r5
    bne 1b
    b op_stop

.macro tmds_encode_1bpp_pal_body shift_instr shamt
	\shift_instr r5, r4, #\shamt
	ands r5, r0 // r0 = mask, equals 0x30
	add r5, r8 // r8 = pal
	ldm r5, {r5, r6, r7}
	str r6, [r2, r3] // r3 = stride
	adds r6, r2, r3
	str r7, [r6, r3]
	stmia r2!, {r5}
.endm

.type op_1bpp_pal, %function
op_1bpp_pal:
    lsls r5, #2
    adds r5, r2
    mov ip, r5
    // TODO: deal with fractions
    ldmia r0!, {r4}
    mov r8, r4
    mov r9, r0
    movs r0, #0x30
1:
	ldmia r1!, {r4}
	tmds_encode_1bpp_pal_body lsls 4
	tmds_encode_1bpp_pal_body lsls 2
	tmds_encode_1bpp_pal_body lsls 0
	tmds_encode_1bpp_pal_body lsrs 2
	tmds_encode_1bpp_pal_body lsrs 4
	tmds_encode_1bpp_pal_body lsrs 6
	tmds_encode_1bpp_pal_body lsrs 8
	tmds_encode_1bpp_pal_body lsrs 10
	tmds_encode_1bpp_pal_body lsrs 12
	tmds_encode_1bpp_pal_body lsrs 14
	tmds_encode_1bpp_pal_body lsrs 16
	tmds_encode_1bpp_pal_body lsrs 18
	tmds_encode_1bpp_pal_body lsrs 20
	tmds_encode_1bpp_pal_body lsrs 22
	tmds_encode_1bpp_pal_body lsrs 24
	tmds_encode_1bpp_pal_body lsrs 26
	cmp r2, ip
	beq 2f
    b 1b
2:
    mov r0, r9
    b op_stop

// r0: scan list in direct threaded format
// r1: input buffer
// r2: output buffer
// r3: stride (of output buffer)
decl_func tmds_scan
    push {r4, r5, r6, r7}
    mov r4, r8
    mov r5, r9
    mov r6, r10
    push {r4, r5, r6}

    // operation, 2x args
    // should count be single pixels or double?
    ldmia r0!, {r4, r5, r6}
    bx r4

decl_func tmds_scan_stop
    pop {r4, r5, r6}
    mov r8, r4
    mov r9, r5
    mov r10, r6
    pop {r4, r5, r6, r7}
    bx lr

// args: count rgb12
decl_func tmds_scan_solid
    mov r8, r1
    lsls r5, #2
    adds r4, r2, r5
    // ip is actual end of output
    mov ip, r4
    lsls r5, #28
    lsrs r5, #28
    adds r4, r2, r5
    // r10 is end of fractional part (may be == r2)
    mov r10, r4
    mov r4, r6
    lsls r4, #28
    lsrs r4, #28
    movs r7, #0x44
    muls r4, r4, r7
    ldr r1, =(tmds_table_4bpp)
    ldr r4, [r1, r4]
    adds r7, r2, r3 // beginning of green row
    cmp r2, r10
    beq 2f
1:
    stmia r2!, {r4}
    cmp r2, r10
    bne 1b
    cmp r2, ip
    beq 4f
2:
    mov r5, r4
3:
    stmia r2!, {r4, r5}
    stmia r2!, {r4, r5}
    cmp r2, ip
    bne 3b
4:

    add ip, r3
    add r10, r3
    lsls r4, r6, #24
    lsrs r4, #28
    // load green pixel from palette
    movs r5, #0x44
    muls r4, r4, r5
    ldr r4, [r1, r4]
    // load red pixel from palette
    lsrs r6, #8
    muls r6, r5
    ldr r6, [r1, r6]
    adds r1, r7, r3 // beginning of red row
    cmp r7, r10
    beq 2f
1:
    stmia r7!, {r4}
    cmp r7, r10
    bne 1b
    cmp r7, ip
    beq 4f
2:
    mov r5, r4
3:
    stmia r7!, {r4, r5}
    stmia r7!, {r4, r5}
    cmp r7, ip
    bne 3b
4:
    // write red
    add ip, r3
    add r10, r3
    cmp r1, r10
    beq 2f
1:
    stmia r1!, {r6}
    cmp r1, r10
    bne 1b
    cmp r1, ip
    beq 4f
2:
    mov r7, r6
3:
    stmia r1!, {r6, r7}
    stmia r1!, {r6, r7}
    cmp r1, ip
    bne 3b
4:
    mov r1, r8
    ldmia r0!, {r4, r5, r6}
    bx r4

// args: count gray4
decl_func tmds_scan_solid_gray
    mov r8, r1
    lsls r5, #2
    adds r4, r2, r5
    // ip is actual end of output
    mov ip, r4
    lsls r5, #28
    lsrs r5, #28
    adds r4, r2, r5
    // r10 is end of fractional part (may be == r2)
    mov r10, r4
    movs r7, #0x44
    muls r6, r6, r7
    ldr r1, =(tmds_table_4bpp)
    ldr r4, [r1, r6]
    adds r7, r2, r3 // beginning of green row
    cmp r2, r10
    beq 2f
1:
    stmia r2!, {r4}
    cmp r2, r10
    bne 1b
    cmp r2, ip
    beq 4f
2:
    mov r5, r4
3:
    stmia r2!, {r4, r5}
    stmia r2!, {r4, r5}
    cmp r2, ip
    bne 3b
4:

    add ip, r3
    add r10, r3
    adds r1, r7, r3 // beginning of red row
    cmp r7, r10
    beq 2f
1:
    stmia r7!, {r4}
    cmp r7, r10
    bne 1b
    cmp r7, ip
    beq 4f
2:
3:
    stmia r7!, {r4, r5}
    stmia r7!, {r4, r5}
    cmp r7, ip
    bne 3b
4:
    // write red
    add ip, r3
    add r10, r3
    cmp r1, r10
    beq 2f
1:
    stmia r1!, {r4}
    cmp r1, r10
    bne 1b
    cmp r1, ip
    beq 4f
2:
3:
    stmia r1!, {r4, r5}
    stmia r1!, {r4, r5}
    cmp r1, ip
    bne 3b
4:
    mov r1, r8
    ldmia r0!, {r4, r5, r6}
    bx r4

// args: count tmds_blue tmds_green tmds_red
// Not sure we'll keep this.
decl_func tmds_scan_solid_tmds
    mov r8, r1
    lsls r5, #2
    adds r4, r2, r5
    // ip is actual end of output
    mov ip, r4
    lsls r5, #28
    lsrs r5, #28
    adds r4, r2, r5
    // r10 is end of fractional part (may be == r2)
    mov r10, r4
    adds r7, r2, r3 // beginning of green row
    cmp r2, r10
    beq 2f
1:
    stmia r2!, {r6}
    cmp r2, r10
    bne 1b
    cmp r2, ip
    beq 4f
2:
    mov r5, r6
3:
    stmia r2!, {r5, r6}
    stmia r2!, {r5, r6}
    cmp r2, ip
    bne 3b
4:

    add ip, r3
    add r10, r3
    ldmia r0!, {r4, r6}
    adds r1, r7, r3 // beginning of red row
    cmp r7, r10
    beq 2f
1:
    stmia r7!, {r4}
    cmp r7, r10
    bne 1b
    cmp r7, ip
    beq 4f
2:
    mov r5, r4
3:
    stmia r7!, {r4, r5}
    stmia r7!, {r4, r5}
    cmp r7, ip
    bne 3b
4:
    // write red
    add ip, r3
    add r10, r3
    cmp r1, r10
    beq 2f
1:
    stmia r1!, {r6}
    cmp r1, r10
    bne 1b
    cmp r1, ip
    beq 4f
2:
    mov r7, r6
3:
    stmia r1!, {r6, r7}
    stmia r1!, {r6, r7}
    cmp r1, ip
    bne 3b
4:
    mov r1, r8
    ldmia r0!, {r4, r5, r6}
    bx r4

.macro tmds_scan_1bpp_pal_body shift_instr shamt
	\shift_instr r5, r4, #\shamt
	ands r5, r0 // r0 = mask, equals 0x30
	add r5, r8 // r8 = pal
	ldm r5, {r5, r6, r7}
	str r6, [r2, r3] // r3 = stride
	adds r6, r2, r3
	str r7, [r6, r3]
	stmia r2!, {r5}
.endm

1:
    b 4f
// args: count pal
decl_func tmds_scan_1bpp_pal
    lsrs r4, r5, #5
    lsls r5, #2
    adds r5, r2
    mov ip, r5 // actual end of output
    mov r8, r6
    mov r9, r0
    lsls r4, #7
    beq 1b
    adds r4, r2
    mov r10, r4 // end of whole part
    movs r0, #0x30
2:
	ldmia r1!, {r4}
	tmds_scan_1bpp_pal_body lsls 4
	tmds_scan_1bpp_pal_body lsls 2
	tmds_scan_1bpp_pal_body lsls 0
	tmds_scan_1bpp_pal_body lsrs 2
	tmds_scan_1bpp_pal_body lsrs 4
	tmds_scan_1bpp_pal_body lsrs 6
	tmds_scan_1bpp_pal_body lsrs 8
	tmds_scan_1bpp_pal_body lsrs 10
	tmds_scan_1bpp_pal_body lsrs 12
	tmds_scan_1bpp_pal_body lsrs 14
	tmds_scan_1bpp_pal_body lsrs 16
	tmds_scan_1bpp_pal_body lsrs 18
	tmds_scan_1bpp_pal_body lsrs 20
	tmds_scan_1bpp_pal_body lsrs 22
	tmds_scan_1bpp_pal_body lsrs 24
	tmds_scan_1bpp_pal_body lsrs 26
	cmp r2, r10
	beq 3f
    b 2b
3:
    cmp r2, ip
    beq 6f
4:
    ldmia r1!, {r4}
    movs r0, #2
5:
	rors r4, r0
    lsrs r5, r4, #30
    lsls r5, #4
	add r5, r8 // r8 = pal
	ldm r5, {r5, r6, r7}
	str r6, [r2, r3] // r3 = stride
	adds r6, r2, r3
	str r7, [r6, r3]
	stmia r2!, {r5}
    cmp r2, ip
    bne 5b
6:
    mov r0, r9
    ldmia r0!, {r4, r5, r6}
    bx r4
