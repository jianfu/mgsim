/*
    Livermore kernel 1 -- Hydro fragment

    double X[1001], Y[1001], Z[1001];
        
    for (k=0 ; k < N; k++)
    {
        x[k] = Q + Y[k] * (R * Z[k+10] + T * Z[k+11]);
    }
*/

    .file "l1_hydro.s"
    .set noat
    .arch ev6
    .text

#
# Constants
#
    .equ Q, 100
    .equ R, 5
    .equ T, 2

#
# Main thread
#
# $27 = address of main
# $10 = N
# $11 = BS
# $12 = PID
#
    .globl main
    .ent main
main:
    ldpc     $27
    ldgp     $29, 0($27)    # $29 = GP
    
    allocate/s  $12, 1, $3   # Start = 0, Step = 1
	allocate/rs $12, 1, $4
	pair $3, $4
	pair $4, $3
	rmtwr $4

	
	setlimit $3, $10        # Limit = N
	setblock $3, $11 
    cred     $3, loop

    mov     Q, $0;
    itoft   $0, $f0;
    cvtqt   $f0, $f0
    fputg   $f0, $3, 0      # $gf0 = Q
    
    mov     R, $0;
    itoft   $0, $f0;
    cvtqt   $f0, $f0
    fputg   $f0, $3, 1      # $gf1 = R
    
    mov     T, $0;
    itoft   $0, $f0;
    cvtqt   $f0, $f0
    fputg   $f0, $3, 2      # $gf2 = T
    
    ldah    $0, X($29)      !gprelhigh
    lda     $0, X($0)       !gprellow
    putg    $0, $3, 0       # $g0 = X

    ldah    $0, Y($29)      !gprelhigh
    lda     $0, Y($0)       !gprellow
    putg    $0, $3, 1       # $g1 = Y

    ldah    $0, Z($29)      !gprelhigh
    lda     $0, Z($0)       !gprellow
    putg    $0, $3, 2       # $g2 = Z
    
    sync     $3, $0        # Sync
    release  $3
    mov      $0, $31


    /*
	#print results
	ldah $2, X($29) !gprelhigh
	lda  $2, X($2)  !gprellow     # $2  = &X[0]
	
	mov $31, $0                   # $0  = i = 0
	
lbody:
	s8addq $0, $2, $1             # $1  = &X[i]
	ldt $f0, 0($1)                # $f0 = X[i]
	
	mov 48,$3
	printf $f0, $3                # print X[i]  (128 for integer)
	
	lda $0, 1($0)                 # $0 = $0 + 1   (i++)
	cmpeq $0, $10, $1             # $1 = (i==N) ? 1 : 0
	beq $1, lbody                 # if(i==0) lbody

lend:	
	nop
	*/
    end
    .end main

#
# Loop thread:
# x[i] = Q + Y[i] * (R * Z[i + 10] + T * Z[i + 11]);
#
# $g0  = &X[0]
# $g1  = &Y[0]
# $g2  = &Z[0]
# $gf0 = Q
# $gf1 = R
# $gf2 = T
# $l0  = i
#
    .globl loop
    .ent loop
    .registers 3 0 2  3 0 3     # GR,SR,LR, GF,SF,LF
loop:
    s8addq  $l0, $g2, $l1   # $l1  = &Z[i]
    ldt     $lf1, 80($l1)   # $lf1 = Z[i + 10]
    ldt     $lf2, 88($l1)   # $lf2 = Z[i + 11]
    s8addq  $l0, $g1, $l1   # $l1  = &Y[i]
    ldt     $lf0,  0($l1)   # $lf0 = Y[i]
    s8addq  $l0, $g0, $l0   # $l0  = &X[i]

    mult    $gf1, $lf1, $lf1; swch  # $lf1 = R * Z[i+10]
    mult    $gf2, $lf2, $lf2; swch  # $lf2 = T * Z[i+11]
    addt    $lf1, $lf2, $lf1; swch  # $lf1 = R * Z[i+10] + T * Z[i+11]
    mult    $lf0, $lf1, $lf0; swch  # $lf0 = Y[i] * (R * Z[i+10] + T * Z[i+11])
    addt    $lf0, $gf0, $lf0; swch  # $lf0 = Q + Y[i] * (R * Z[i+10] + T * Z[i+11])
    stt     $lf0, 0($l0)            # X[i] = $lf0
    end   
    .end loop

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:128\0"

#
# Data
#
    .section .bss
    .align 6
X:  .skip 2501 * 8
    .align 6
Y:  .skip 2501 * 8
    .align 6
Z:  .skip 2501 * 8
