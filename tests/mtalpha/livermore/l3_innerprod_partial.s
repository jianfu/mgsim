# Livermore kernel 3 -- Inner product, with partial reduction
#
    .file "l3_innerprod_partial.s"
    .set noat
    .arch ev6

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:512\0"
	
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
    
    # Family runs from [0 ... #procs - 1]
	mov 5, $30   #PID=2, Size=1
    allocate/s $30, 1, $5
    
    getpid   $3
    negq     $3, $4
    and      $3, $4, $3     # $3 = place.size
    setlimit $5, $3
	setblock $5, $11
	
    cred    $5, outer
    
    ldah    $0, X($29)      !gprelhigh
    lda     $0, X($0)       !gprellow
    putg    $0, $5, 0       # $g0 = X

    ldah    $0, Y($29)      !gprelhigh
    lda     $0, Y($0)       !gprellow
    putg    $0, $5, 1       # $g1 = Y
    
	putg    $12, $5, 4
	
    # Calculate N / #procs
    divqu    $10, $3, $2
    putg     $2,  $5, 2     # $g2 = normal_size = N / #procs
    
    # Calculate #procs that does one more
    mull    $2,  $3, $3
    subl    $10, $3, $3
    putg    $3, $5, 3       # $g3 = num_more = N % #procs = N - normal_size * #procs
    
    fputs   $f31, $5, 0     # $df0 = sum = 0.0
    
    sync    $5, $0
    release $5
    mov     $0, $31
    end
    .end main

#
# Outer loop thread
#
# $g0  = X
# $g1  = Y
# $g2  = normal_size
# $g3  = num_more
# $df0 = sum
# $l0  = i (0.. #procs - 1)
    .globl outer
    .ent outer
    .registers 5 0 5 0 1 1
outer:
    mulq    $l0, $g2, $l1       # $l1 = start
    addq    $g2,   1, $l2       # $l2 = more_size = normal_size + 1
    
    mov     $l0, $l4
    subq    $l4, $g3, $l3
    cmovgt  $l3, $g3, $l4       # $l4 = min(i, num_more)
    addq    $l1, $l4, $l1       # $l1 = actual start (accounting for +1)
    cmovge  $l3, $g2, $l2       # $l2 = actual size  (accounting for +1)
    addq    $l1, $l2, $l2       # $l2 = limit
    
    getpid  $l3
    subq    $l3, 1, $l4
    and     $l3, $l4, $l3
    sll     $l0, 1, $l0
    or      $l3, $l0
    or     $l3, 1, $l3         # $l3 = PID for i-th core in the place
	
	
    
    allocate/s $l3, 0, $l3
    setstart $l3, $l1
    setlimit $l3, $l2
	setblock $l3, $g4
    cred     $l3, loop
    
    putg $g0, $l3, 0            # $g0 = X
    putg $g1, $l3, 1            # $g1 = Y
    fputs $f31, $l3, 0          # $df0 = sum = 0.0
    
    sync    $l3, $l0
    mov     $l0, $31; swch
    fgets   $l3, 0, $lf0        # $lf0 = {$sf0} = sum
    release $l3
    addt    $df0, $lf0, $lf0
    swch
    fmov    $lf0, $sf0
    end
    .end outer

#
# Local loop thread
#
# $g0  = X
# $g1  = Y
# $df0 = sum
# $l0  = i
    .globl loop
    .ent loop
    .registers 2 0 2 0 1 2
loop:
    s8addq $l0, $g1, $l1
    ldt $lf1, 0($l1)
    s8addq $l0, $g0, $l0
    ldt $lf0, 0($l0)
    mult $lf0, $lf1, $lf0; swch
    addt $df0, $lf0, $lf0; swch
    fmov $lf0, $sf0
    end
    .end loop

    .section .bss
    .align 6
X:  .skip 10001 * 8
    .align 6
Y:  .skip 10001 * 8
