.data
	a0	word	20
	a1	word	10
	a2	word	15
	a3	word	17
	a4	word	18
	a5	word	21
	a6	word	5
	a7	word	30
	a8	word	7
	a9	word	12
	a10	word	3
	a11	word 	22
	a12	word	14
	a13	word	31
	a14	word	19
	a15	word	4
	a16	word	33
	a17	word	48
	a18	word	37
	a19	word	1

.text
	addi	%R6, %R0, 80
	addi	%R7, %R0, 76
	laddr	%R1, a0
@1:
	addi	%R2, %R1, 4
@2:
	lw	%R3, 0(%R1)
	lw	%R4, 0(%R2)
	slt	%R5, %R4, %R3
	beq	%R5, %R0, @3
	sw	%R4, 0(%R1)
	sw	%R3, 0(%R2)
@3:
	addi	%R2, %R2, 4
	slt	%R5, %R2, %R6
	bne	%R5, %R0, @2
	addi	%R1, %R1, 4
	slt	%R5, %R1, %R7
	bne	%R5, %R0, @1

