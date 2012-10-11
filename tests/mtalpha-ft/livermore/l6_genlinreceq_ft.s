/*
    Livermore kernel 6 -- General linear recurrence equations

    double x[1001], y[64][64];

    for(int i = 1; i < N; i++) {
        double sum = 0;
        for(int j = 0; j < i; j++) {
            sum += y[j][i] * x[i - j - 1];
        }
        x[i] = x[i] + sum;
    }
*/
    .file "l6_genlinreceq.s"
    .set noat
    .arch ev6

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:16\0"
	
    .text
#
# Main thread
#
# $27 = address of main
# $10 = N
#
    .globl main
    .ent main
main:
    ldpc     $27
    ldgp    $29, 0($27)
    
	mov  5, $30
    allocate/s $30, 1, $4
	allocate/rs $30, 1, $20
	pair $4, $20
	pair $20, $4
	rmtwr $20
	
	
    setstart $4, 1
    setlimit $4, $10
    setblock $4, $11
    cred    $4, outer
    
    ldah    $0, X($29)      !gprelhigh
    lda     $0, X($0)       !gprellow
    putg    $0, $4, 0       # $g0 = X

    ldah    $0, Y($29)      !gprelhigh
    lda     $0, Y($0)       !gprellow
    putg    $0, $4, 1       # $g1 = Y
    
	putg 129, $4, 2
	putg $12, $4, 3
	
    puts    $31, $4, 0      # $d0 = token
    
    sync    $4, $0
    release $4
    mov     $0, $31
    end
    .end main
    
#
# Outer loop
#
# $g0  = X
# $g1  = Y
# $d0  = token
# $l0  = i
#
    .ent outer
    .registers 4 1 7 0 0 2    
outer:
    allocate/s $g2, 1, $l3
	allocate/rs $g2, 1, $l6
	pair $l3, $l6
	pair $l6, $l3
	rmtwr $l6
	
    setlimit $l3, $l0; swch
	setblock $l3, $g3
    mov     $d0, $31; swch
    cred    $l3, inner
    
    putg    $l0, $l3, 0     # $g0  = i
    swch

    s8addq  $l0,  $g1, $l1
    putg    $l1, $l3, 1     # $g1  = &Y[0][i]
    
    putg    $g0, $l3, 2     # $g2  = X
    
    fputs   $f31, $l3, 0    # $df0 = sum = 0.0
    
    s8addq  $l0, $g0, $l4   # $l4 = &X[i]
    ldt     $lf1, 0($l4)
    
    sync    $l3, $l0
    mov     $l0, $31
    fgets   $l3, 0, $lf0    # $sf0
    release $l3
    
    addt    $lf1, $lf0, $lf0; swch  # $lf0 = X[i] + sum
    stt     $lf0, 0($l4); swch
    wmb
    clr     $s0
    end
    .end outer
    
#
# Inner loop
#
# $g0  = i
# $g1  = &Y[0][i]
# $g2  = X
# $df0 = sum
# $l0  = j
#
    .ent inner
    .registers 3 0 2 0 1 2
inner:
    subq    $g0,  $l0, $l1
    s8addq  $l1,  $g2, $l1
    ldt     $lf0, -8($l1)       # $lf0 = X[i - j - 1]
    
    sll     $l0,   6, $l0
    s8addq  $l0, $g1, $l0
    ldt     $lf1, 0($l0)        # $lf1 = Y[j][i]
    
    mult    $lf0, $lf1, $lf0; swch
    addt    $lf0, $df0, $lf0; swch
    fmov    $lf0, $sf0
    end
    .end inner
    
    .section .bss
    .align 6
X:  .skip 10001 * 8
    .align 6
Y:  .skip 128 * 128 * 8
