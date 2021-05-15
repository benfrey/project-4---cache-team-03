	lw	1	0	five	# R[1] = M[0 + 7] = 5
	lw	2	1	3	# R[2] = M[5 + 3] = -1
start	add	1	1	2	# R[1] = R[1] + R[2]
	beq	0	1	2	# Go to line 6 if R[1] == R[0]
	beq	0	0	start	# Go to start of loop
	noop				# No operation, inc PC
	halt				# End program
five	.fill	5			# Fill label five with 5
neg1	.fill	-1			# Fill label neg1 with -1
staddr	.fill	start			# Fill staddr with start
