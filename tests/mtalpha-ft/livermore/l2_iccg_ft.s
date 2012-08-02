/*
    Livermore kernel 2 -- ICCG (Incomplete Cholesky Conjugate Gradient)

    double x[1001], v[1001];
    
    int ipntp = 0;
    for (int m = log2(n); m >= 0; m--)
    {
        int ii   = 1 << m;
        int ipnt = ipntp;
        ipntp    = ipntp + ii;

        for (int i = 1; i < ii; i+=2)
        {
            int k = ipnt + i;
            x[ipntp + i / 2] = x[k] - (v[k]*x[k-1] + v[k+1]*x[k+1]);
        }
    }
*/
    .file "l2_iccg.s"
    .set noat

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:8\0"

    .text   
#
# Main thread
#
# $27 = address of main
# $10 = M (= log2 N)
#
# outer (dependent)
# PID = 2; Size = 1
# $11 = BS
#
# inner (independent)
# $12 = PID
# $13 = BS

    .global main
    .ent main
main:
    ldpc $27
    ldah    $29, 0($27)     !gpdisp!1
    lda     $29, 0($29)     !gpdisp!1   # $29 = GP
    
    negq    1, $5
	
	mov 5, $30
    allocate/s  $30, 1, $4
	allocate/rs $30, 1, $20
	pair  $4,  $20
	pair  $20, $4
	rmtwr $20
	
	
	
    setstart $4, $10
    setlimit $4, $5
    setstep  $4, $5
	setblock $4, $11
    cred    $4, outer
    
    ldah    $0, X($29)      !gprelhigh
    lda     $0, X($0)       !gprellow
    putg    $0, $4, 0       # $g0 = X
    
    ldah    $0, V($29)      !gprelhigh
    lda     $0, V($0)       !gprellow
    putg    $0, $4, 1       # $g1 = V
    
    putg    1,  $4, 2       # $g2 = 1
    puts    0,  $4, 0       # $d0 = 0
	
	putg    $12, $4, 3
	putg    $13, $4, 4
    
    sync    $4, $0
    release $4
    mov     $0, $31
	
	
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
# Outer thread
#
# $g0 = X
# $g1 = V
# $g2 = 1
# $d0 = ipntp
# $l0 = m
#
    .ent outer
    .registers 5 1 4 0 0 0
outer:
    allocate/s  $g3, 1, $l1
	allocate/rs $g3, 1, $l3
	pair  $l1, $l3
	pair  $l3, $l1
	rmtwr $l3
	
	
    sll      $g2, $l0, $l0       # $l0 = 1 << m = ii
    setlimit $l1, $l0; swch
    setstart $l1, 1
    setstep  $l1, 2
	setblock $l1, $g4
    addq     $d0, $l0, $l0; swch # $l0 = ipntp
    cred     $l1, inner
    putg     $g0, $l1, 0         # $g0 = X
    putg     $g1, $l1, 1         # $g1 = V
    putg     $l0, $l1, 2         # $g2 = ipntp
    putg     $d0, $l1, 3         # $g3 = ipnt
    sync     $l1, $l2
    release  $l1; swch
    mov      $l2, $31; swch
    mov      $l0, $s0            # $s0 = ipntp
    end
    .end outer

#
# Inner thread
#
# $g0 = X
# $g1 = V
# $g2 = ipntp
# $g3 = ipnt
# $l0 = i
#
    .ent inner
    .registers 4 0 3 0 0 5
inner:
    addl    $g3, $l0, $l2   # $l2 = k = ipnt + i
    s8addq  $l2, $g1, $l1   # $l1 = &V[k]
    ldt     $lf1, 0($l1)    # $lf1 = V[k]
    s8addq  $l2, $g0, $l2   # $l2 = &X[k]
    ldt     $lf4,-8($l2)    # $lf4 = X[k-1]
    ldt     $lf2, 8($l1)    # $lf2 = V[k+1]
    ldt     $lf3, 8($l2)    # $lf3 = X[k+1]
    ldt     $lf0, 0($l2)    # $lf0 = X[k]
    srl     $l0,   1, $l0
    addq    $g2, $l0, $l0
    s8addq  $l0, $g0, $l0   # $l0 = &X[ipntp + i / 2]
    mult    $lf1, $lf4, $lf1; swch
    mult    $lf2, $lf3, $lf2; swch
    addt    $lf1, $lf2, $lf1; swch
    subt    $lf0, $lf1, $lf1; swch
    stt     $lf1, 0($l0)
    end
    .end inner

    .section .bss   
    .align 6
X:  .skip 100001 * 8
    .align 6
V:  .skip 100001 * 8
