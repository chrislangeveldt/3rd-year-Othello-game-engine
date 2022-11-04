# Chris Langeveldt - Othello Project
## Features
- I have implemented a minimax algorithm with alpha beta pruning
- Multiple processes each perform this algorithm
- Work is being dynamically allocated from process 0
- Alpha values are shared between the non zero processes
- For Evaluation, I use a combination of Stability, Corners, Coins and Mobility
- Iterative deepening is implemented 
- Moves are also ordered by the static evaluation board before distributed to other processes

## Note
The following is the case when running on my, somewhat useless, laptop:

Without Stability activated, the program runs somewhat smoothly on a starting depth of about 9 with iterative deepening.
However, with Stability activated, it runs somewhat smoothly on a starting depth of 7 with iterative deepening.



## Original Project Instructions

Make a copy of random.c and rename 

Execute
1. in a terminal window (when using default ssh)
make 
. runInOneWindow.sh

OR 
1. in a terminal window (on your local machine)
make
. runall.sh

OR 
1. in a terminal window, 
make 
. runserver.sh 

2. in a second terminal window,
. runlobby.sh

3. also in the second terminal window (or in a separate window),
. runplayer1.sh 

4. and in a third terminal window
. runplayer2.sh

