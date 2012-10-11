/*
 This test does a continuation test.
 It creates recursively a load of families that would, without continuations, cause deadlock.
 */
    .file "continuation.s"
    .set noat
    .text

    .globl main
    .ent main
main:
    allocate/s $31, 1, $2        # Default
	####
	allocate/rs $31, 1, $3
	pair $2, $3
	pair $3, $2
	rmtwr $3
	####
    cred     $2, bar
    putg     255, $2, 0
    detach   $2
    end    
    .end main

    .ent bar
    .registers 1 0 3 0 0 0
bar:
    # Stop if $g0 is 0
    bne      $g0, 1f
    nop
    end
1:    

    allocate/s $31, 1, $l0       # Default
	allocate/rs $31, 1, $l2
	pair $l0, $l2
	pair $l2, $l0
	rmtwr $l2

    cred     $l0, bar
    swch
    subq     $g0, 1, $l1    # Pass on $g0 - 1
    putg     $l1, $l0, 0
    swch
    
    # Delay for $l1 loops to tie up resources
2:  beq $l1, 3f
    subq $l1, 1, $l1
    br 2b
3:

    detach   $l0
    end
    .end bar

    .data
    .ascii "PLACES: 16\0"
