/* vim: :se ai :se sw=4 :se ts=4 :se sts :se et */

/*H**********************************************************************
 *
 *    Auhtor: Chris Langeveldt 
 *    Project: Computer Science 314
 *
 *    The communication with the referee is handled by an implementaiton of comms.h,
 *    All communication is performed at rank 0.
 *
 *   
 *H***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <mpi.h>
#include <time.h>
#include <assert.h>
#include "comms.h"

#define STARTING_MAX_DEPTH 7 	// If stability is not used, this depth can be pushed to about 9
#define MAX_DEPTH 15			// when iterative deepening stops
#define MAX_TIME 4

#define REQUEST_MOVE_TAG 0
#define SEND_MOVE_TAG 1
#define NO_MOVES_LEFT_TAG 2
#define SEND_ALPHA_TAG 3
#define TIMEOUT_TAG 4

// Stability stuff
const int UNSTABLE 	 = 0;
const int H_BORDER 	 = 1;
const int V_BORDER 	 = 2;
const int UDD_BORDER = 4; // up down diagional
const int DUD_BORDER = 8;
const int STABLE 	= 15;

#define IS_UNSTABLE(type) 		(type == UNSTABLE)
#define IS_H_BORDER(type) 		(type & H_BORDER)
#define IS_V_BORDER(type) 		(type & V_BORDER)
#define IS_UDD_BORDER(type) 	(type & UDD_BORDER)
#define IS_DUD_BORDER(type) 	(type & DUD_BORDER)
#define IS_STABLE(type) 		(type == STABLE)

// When a loop is done in spiral for stability
#define IS_LOOP_COMPLETED(loc)	(loc == 21 || loc == 32 || loc == 43 || loc == 54) 

// Spiral used to iterate through stability board 
const int spiral[64] = {	11, 12, 13, 14, 15, 16, 17, 18, 
							28, 38, 48, 58, 68, 78, 88, 
							87, 86, 85, 84, 83, 82, 81, 
							71, 61, 51, 41, 31, 21,
							
							22, 23, 24, 25, 26, 27,
							37, 47, 57, 67, 77,
							76, 75, 74, 73, 72,
							62, 52, 42, 32,

							33, 34, 35, 36,
							46, 56, 66,
							65, 64, 63,
							53, 43,

							44, 45,
							55, 54 
						}; 
// Static position evaluation for move ordering
const int eval_board[90] = {0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
							0,  4, -3,  2,  2,  2,  2, -3,  4, 0,
							0, -3, -4, -1, -1, -1, -1, -4, -3, 0,
							0,  2, -1,  1,  0,  0,  1, -1,  2, 0,
							0,  2, -1,  0,  1,  1,  0, -1,  2, 0,
							0,  2, -1,  0,  1,  1,  0, -1,  2, 0,
							0,  2, -1,  1,  0,  0,  1, -1,  2, 0,
							0, -3, -4, -1, -1, -1, -1, -4, -3, 0,
							0,  4, -3,  2,  2,  2,  2, -3,  4, 0,};

const int TRUE = 1;
const int FALSE = 0;

const int EMPTY = 0;
const int BLACK = 1;
const int WHITE = 2;

const int OUTER = 3;
const int ALLDIRECTIONS[8] = {-11, -10, -9, -1, 1, 9, 10, 11};
const int BOARDSIZE = 100;

const int LEGALMOVSBUFSIZE = 65;
const char piecenames[4] = {'.','b','w','?'};

void run_master(int argc, char *argv[]);
int initialise_master(int argc, char *argv[], int *time_limit, int *my_colour, FILE **fp);
void gen_move_master(char *move, int my_colour, FILE *fp);
void apply_opp_move(char *move, int my_colour, FILE *fp);
void game_over();
void run_worker();
void initialise_board();
void free_board();

void legal_moves(int player, int *moves, FILE *fp);
int legalp(int move, int player, FILE *fp);
int validp(int move);
int would_flip(int move, int dir, int player, FILE *fp);
int opponent(int player, FILE *fp);
int find_bracket_piece(int square, int dir, int player, FILE *fp);
int strategy(int my_colour, FILE *fp);
void make_move(int move, int player, FILE *fp);
void make_flips(int move, int dir, int player, FILE *fp);
int get_loc(char* movestring);
void get_move_string(int loc, char *ms);
void print_board(FILE *fp);
char nameof(int piece);
int count(int player, int * board);
void copy_board(int *original, int *copy);
int minimax(int current_colour, int depth, int alpha, int beta);

int *board;
clock_t start, end;
int max_colour;
int timeout;

int main(int argc, char *argv[]) {
	int rank;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
 
	initialise_board(); //one for each process

	if (rank == 0) {
	    run_master(argc, argv);
	} else {
	    run_worker(rank);
	}
	game_over();
}

void run_master(int argc, char *argv[]) {
	char cmd[CMDBUFSIZE];
	char my_move[MOVEBUFSIZE];
	char opponent_move[MOVEBUFSIZE];
	int time_limit;
	int my_colour;
	int running = 0;
	FILE *fp = NULL;

	if (initialise_master(argc, argv, &time_limit, &my_colour, &fp) != FAILURE) {
		running = 1;
	}
	if (my_colour == EMPTY) my_colour = BLACK;
	// Broadcast my_colour
	MPI_Bcast(&my_colour, 1, MPI_INT, 0, MPI_COMM_WORLD);
	max_colour = my_colour;

	while (running == 1) {
		/* Receive next command from referee */
		if (comms_get_cmd(cmd, opponent_move) == FAILURE) {
			fprintf(fp, "Error getting cmd\n");
			fflush(fp);
			running = 0;
			break;
		}

		/* Received game_over message */
		if (strcmp(cmd, "game_over") == 0) {
			running = 0;
			fprintf(fp, "Game over\n");
			fflush(fp);
			break;

		/* Received gen_move message */
		} else if (strcmp(cmd, "gen_move") == 0) {
			// Broadcast running
			MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD);
			// Broadcast board 
			MPI_Bcast(board, BOARDSIZE, MPI_INT, 0, MPI_COMM_WORLD);

			gen_move_master(my_move, my_colour, fp);
			print_board(fp);

			if (comms_send_move(my_move) == FAILURE) {
				running = 0;
				fprintf(fp, "Move send failed\n");
				fflush(fp);
				break;
			}
		/* Received opponent's move (play_move mesage) */
		} else if (strcmp(cmd, "play_move") == 0) {
			apply_opp_move(opponent_move, my_colour, fp);
			print_board(fp);

		/* Received unknown message */
		} else {
			fprintf(fp, "Received unknown command from referee\n");
		}
	}
	// Broadcast running
	MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD);
}

int initialise_master(int argc, char *argv[], int *time_limit, int *my_colour, FILE **fp) {
	int result = FAILURE;

	if (argc == 5) { 
		unsigned long ip = inet_addr(argv[1]);
		int port = atoi(argv[2]);
		*time_limit = atoi(argv[3]);

		*fp = fopen(argv[4], "w");
		if (*fp != NULL) {
			fprintf(*fp, "Initialise communication and get player colour \n");
			if (comms_init_network(my_colour, ip, port) != FAILURE) {
				result = SUCCESS;
			}
			fflush(*fp);
		} else {
			fprintf(stderr, "File %s could not be opened", argv[4]);
		}
	} else {
		fprintf(*fp, "Arguments: <ip> <port> <time_limit> <filename> \n");
	}
	
	return result;
}

void initialise_board() {
	int i;
	board = (int *) malloc(BOARDSIZE * sizeof(int));
	for (i = 0; i <= 9; i++) board[i] = OUTER;
	for (i = 10; i <= 89; i++) {
		if (i%10 >= 1 && i%10 <= 8) board[i] = EMPTY; else board[i] = OUTER;
	}
	for (i = 90; i <= 99; i++) board[i] = OUTER;
	board[44] = WHITE; board[45] = BLACK; board[54] = BLACK; board[55] = WHITE;
}

void free_board() {
	free(board);
}

/**
 *   Rank i (i != 0) executes this code 
 *   ----------------------------------
 *   Called at the start of execution on all ranks except for rank 0.
 *   - run_worker should play minimax from its move(s) 
 *   - results should be send to Rank 0 for final selection of a move 
 */
void run_worker() {
	int running = 0, my_colour;
	int no_moves_left;
	int move, eval, flag;
	int alpha, other_alpha;
	int buffer;
	int i, comm_sz, my_rank, depth;
	int *best_move = (int *) calloc(2, sizeof(int));
	int *board_copy= (int *) calloc(BOARDSIZE, sizeof(int));
	MPI_Request request;
	MPI_Status status;

	MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

	// Broadcast colour
	MPI_Bcast(&my_colour, 1, MPI_INT, 0, MPI_COMM_WORLD);
	max_colour = my_colour;
	// Broadcast running
	MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD);

	while (running == 1) {
		// Broadcast board
		MPI_Bcast(board, BOARDSIZE, MPI_INT, 0, MPI_COMM_WORLD);
		
		copy_board(board, board_copy);

		depth = STARTING_MAX_DEPTH-1;
		timeout = FALSE;
	
		// Iterative deepening loop
		while (!timeout) {
			best_move[0] = -1;		 // move
			best_move[1] = -1000000; // eval
			alpha = -1000000;
			no_moves_left = FALSE;

			// request a move from master; also send 0 because a move evaluation has been completed
			MPI_Isend(&FALSE, 1, MPI_INT, 0, REQUEST_MOVE_TAG, MPI_COMM_WORLD, &request); 
			while (!no_moves_left && !timeout) { 
				// Wait for message from master
				MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
				switch (status.MPI_TAG) {
					case SEND_MOVE_TAG: 
						MPI_Recv(&move, 1, MPI_INT, 0, SEND_MOVE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
						
						// Evaluating the move 
						make_move(move, my_colour, NULL);
						eval = minimax(opponent(my_colour, NULL), depth, alpha, 1000000);
						if (timeout) break;
						copy_board(board_copy, board);
						if (eval > best_move[1]) {
							best_move[0] = move;
							best_move[1] = eval;
						}
						// Sharing alpha values
						if (best_move[1] > alpha) {
							alpha = best_move[1];
							for (i = 1; i < comm_sz; i++) { 
								if (i == my_rank) continue;
								MPI_Isend(&alpha, 1, MPI_INT, i, SEND_ALPHA_TAG, MPI_COMM_WORLD, &request);
							}
						}
						// Request move when done; also send 1 because a move evaluation has been completed
						MPI_Isend(&TRUE, 1, MPI_INT, 0, REQUEST_MOVE_TAG, MPI_COMM_WORLD, &request);
						break;
					
					case SEND_ALPHA_TAG: 
						MPI_Recv(&other_alpha, 1, MPI_INT, status.MPI_SOURCE, SEND_ALPHA_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
						if (other_alpha > alpha) {
							alpha = other_alpha;
						}
						break;

					case NO_MOVES_LEFT_TAG: 
						MPI_Recv(&no_moves_left, 1, MPI_INT, 0, NO_MOVES_LEFT_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
						break;

					case TIMEOUT_TAG: 
						MPI_Recv(&timeout, 1, MPI_INT, 0, TIMEOUT_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
						break;
				}
			}    
			// Barrier to make sure I catch all unreceived sends
			MPI_Barrier(MPI_COMM_WORLD);
			// catch unreceived messages
			flag = TRUE; 
			while (flag) { 
				MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
				if (flag) {
					MPI_Recv(&buffer, 1, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
					if (status.MPI_TAG == TIMEOUT_TAG) { // If one happens to timeout, set vairaible
						timeout = TRUE;
					}
				}
			}
			// Send best move to master rank
			MPI_Gather(best_move, 2, MPI_INT, NULL, 0, MPI_INT, 0, MPI_COMM_WORLD); // send best move and eval
			depth++;
		}
		// Broadcast running
		MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD); 
	}
	free(best_move);
	free(board_copy);
}

/**
 *  Rank 0 executes this code: 
 *  --------------------------
 *  Called when the next move should be generated 
 *  - gen_move_master should play minimax from its move(s)
 *  - the ranks may communicate during execution 
 *  - final results should be gathered at rank 0 for final selection of a move 
 */
void gen_move_master(char *move, int my_colour, FILE *fp) {
	int loc;

	/* generate move */
	loc = strategy(my_colour, fp);

	if (loc == -1) {
		strncpy(move, "pass\n", MOVEBUFSIZE);
	} else {
		/* apply move */
		get_move_string(loc, move);
		make_move(loc, my_colour, fp);
	}
}

void apply_opp_move(char *move, int my_colour, FILE *fp) {
	int loc;
	if (strcmp(move, "pass\n") == 0) {
		return;
	}
	loc = get_loc(move);
	make_move(loc, opponent(my_colour, fp), fp);
}

void game_over() {
	free_board();
	MPI_Finalize();
}

void get_move_string(int loc, char *ms) {
	int row, col, new_loc;
	new_loc = loc - (9 + 2 * (loc / 10));
	row = new_loc / 8;
	col = new_loc % 8;
	ms[0] = row + '0';
	ms[1] = col + '0';
	ms[2] = '\n';
	ms[3] = 0;
}

int get_loc(char* movestring) {
	int row, col;
	/* movestring of form "xy", x = row and y = column */ 
	row = movestring[0] - '0'; 
	col = movestring[1] - '0'; 
	return (10 * (row + 1)) + col + 1;
}

void legal_moves(int player, int *moves, FILE *fp) {
	int move, i;
	moves[0] = 0;
	i = 0;
	for (move = 11; move <= 88; move++)
		if (legalp(move, player, fp)) {
		i++;
		moves[i] = move;
	}
	moves[0] = i;
}

int legalp(int move, int player, FILE *fp) {
	int i;
	if (!validp(move)) return 0;
	if (board[move] == EMPTY) {
		i = 0;
		while (i <= 7 && !would_flip(move, ALLDIRECTIONS[i], player, fp)) i++;
		if (i == 8) return 0; else return 1;
	}
	else return 0;
}

int validp(int move) {
	if ((move >= 11) && (move <= 88) && (move%10 >= 1) && (move%10 <= 8))
		return 1;
	else return 0;
}

int would_flip(int move, int dir, int player, FILE *fp) {
	int c;
	c = move + dir;
	if (board[c] == opponent(player, fp))
		return find_bracket_piece(c+dir, dir, player, fp);
	else return 0;
}

int find_bracket_piece(int square, int dir, int player, FILE *fp) {
	while (board[square] == opponent(player, fp)) square = square + dir;
	if (board[square] == player) return square;
	else return 0;
}

int opponent(int player, FILE *fp) {
	if (player == BLACK) return WHITE;
	if (player == WHITE) return BLACK;
	fprintf(fp, "illegal player\n"); return EMPTY;
}

/**
 *  Rank 0 executes this code: 
 *  --------------------------
 *  Called when best move should be calculated 
 *  - Work is dynamically allocated from here as other processes request work.
 *  - This is also where timeout is checked for iterative deepening 
 */
int strategy(int my_colour, FILE *fp) {
	int i, j, a = 0, requests, moves_completed, depth;
	int comm_sz, flag, buffer;
	int best_move[2] = {-1, -1000000};
	int *best_moves;
	int *moves = (int *) calloc(LEGALMOVSBUFSIZE, sizeof(int));
	double time_spent = 0, time_spent_on_depth = 0;
	clock_t temp_start;
	MPI_Status status;
	MPI_Request request;

	// start timer for iterative deepening
	start = clock(); 

	MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);
	best_moves = (int *) calloc(comm_sz*2, sizeof(int));

	legal_moves(my_colour, moves, fp);

	// Sort moves by static evaluation board
	for (i = 1; i <= moves[0]; i++) {
		for (j = i; j <= moves[0]; j++) {
			if (eval_board[moves[j]] > eval_board[moves[i]]) {
				a = moves[i];
				moves[i] = moves[j];
				moves[j] = a;
			}
		}
	}	
	depth = STARTING_MAX_DEPTH-1;
	timeout = FALSE;
	// Iterative deepening loop
	while (!timeout) {
		requests = 0;
		moves_completed = 0;

		// Start timer to find time taken at this depth
		temp_start = clock();

		// This loop dynamicly allocates moves to processes
		while (moves_completed < moves[0] && !timeout && moves[0] > 1) { // exit only when all moves have been EVALUATED or timeout
			// probe for move request
			MPI_Iprobe(MPI_ANY_SOURCE, REQUEST_MOVE_TAG, MPI_COMM_WORLD, &flag, &status); // look for move request
			if (flag) {
				// receive move request
				MPI_Recv(&buffer, 1, MPI_INT, status.MPI_SOURCE, REQUEST_MOVE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				moves_completed += buffer; // If buffer == 1 then evaluation of a move has been completed
				requests++;
				if (requests <= moves[0]) {
					// send move
					MPI_Isend(&moves[requests], 1, MPI_INT, status.MPI_SOURCE, SEND_MOVE_TAG, MPI_COMM_WORLD, &request); // send unevaluated move
				} 
			}
			end = clock();
			time_spent = (double)(end - start) / CLOCKS_PER_SEC;
			// If all moves have been evaluated and the time taken at current depth
			// is less than the time remaining, then going deeper is pointless
			if (moves_completed >= moves[0]) {
				end = clock();
				time_spent_on_depth = (double)(end - temp_start) / CLOCKS_PER_SEC;
				if (time_spent_on_depth + time_spent >= MAX_TIME-0.1) {
					depth = MAX_DEPTH; // Set to enter following if statement
				}
			}
			// Check cut off time for iterative deeping
			if (time_spent > MAX_TIME-0.1 || depth >= MAX_DEPTH) { // also cap depth
				for (i = 1; i < comm_sz; i++) { 
					MPI_Isend(&TRUE, 1, MPI_INT, i, TIMEOUT_TAG, MPI_COMM_WORLD, &request);
				}
				timeout = TRUE;
			}
		}
		// If there is only 0 or 1 moves, we skip the process above
		if (moves[0] <= 1) {
			for (i = 1; i < comm_sz; i++) { 
				MPI_Isend(&TRUE, 1, MPI_INT, i, TIMEOUT_TAG, MPI_COMM_WORLD, &request);
			}
			timeout = TRUE;
		}
		// let workers know that no moves left
		if (!timeout) {
			for (i = 1; i < comm_sz; i++) { 
				MPI_Isend(&TRUE, 1, MPI_INT, i, NO_MOVES_LEFT_TAG, MPI_COMM_WORLD, &request);
			}
		}
		// Barrier to make sure I catch all unreceived sends
		MPI_Barrier(MPI_COMM_WORLD);
		// catch unreceived message
		flag = TRUE; 
		while (flag) {
			MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
			if (flag) {
				MPI_Recv(&buffer, 1, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			}
		}
		// revceive best move from each worker
		MPI_Gather(MPI_IN_PLACE, 2, MPI_INT, best_moves, 2, MPI_INT, 0, MPI_COMM_WORLD); 
		// get best move
		for (i = 2; i < comm_sz*2; i+=2) { //index 0 and 1 are for master process;
			if (best_moves[i] == -1) continue; 
			if (best_moves[i+1] > best_move[1]) {
				best_move[0] = best_moves[i];
				best_move[1] = best_moves[i+1];
			}
		}
		depth++;
	}
	// failsafe for if time runs out before best move can be calculated
	if (moves[0] != 0 && best_move[0] == -1) {
		best_move[0] = moves[1];
	}
	if (moves[0] == 1) best_move[0] = moves[1];
	free(moves);
	free(best_moves);
	return(best_move[0]);
}

void make_move(int move, int player, FILE *fp) {
	int i;
	board[move] = player;
	for (i = 0; i <= 7; i++) make_flips(move, ALLDIRECTIONS[i], player, fp);
}

void make_flips(int move, int dir, int player, FILE *fp) {
	int bracketer, c;
	bracketer = would_flip(move, dir, player, fp);
	if (bracketer) {
		c = move + dir;
		do {
			board[c] = player;
			c = c + dir;
		} while (c != bracketer);
	}
}

void print_board(FILE *fp) {
	int row, col;
	fprintf(fp, "   1 2 3 4 5 6 7 8 [%c=%d %c=%d]\n",
		nameof(BLACK), count(BLACK, board), nameof(WHITE), count(WHITE, board));
	for (row = 1; row <= 8; row++) {
		fprintf(fp, "%d  ", row);
		for (col = 1; col <= 8; col++)
			fprintf(fp, "%c ", nameof(board[col + (10 * row)]));
		fprintf(fp, "\n");
	}
	fflush(fp);
}

char nameof(int piece) {
	assert(0 <= piece && piece < 5);
	return(piecenames[piece]);
}

int count(int player, int * board) {
	int i, cnt;
	cnt = 0;
	for (i = 1; i <= 88; i++)
		if (board[i] == player) cnt++;
	return cnt;
}

FILE* open_logfile() {
    FILE* fptr = NULL;
    char* filename = NULL;
    int rank;

    if (!filename) {
        filename = malloc(64*sizeof(char));
        if (filename) {
            MPI_Comm_rank(MPI_COMM_WORLD, &rank);
            sprintf(filename,"process%d.log", rank);
        } else {
            return NULL;
        }
    }

    fptr = fopen(filename, "a");
    if (!fptr)
        return NULL; 

    free(filename);
    return fptr;
}

void log_msg(char *msg) { 
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    FILE *fptr = open_logfile();
    fprintf(fptr,"%s", msg);
    fclose(fptr);
}

void copy_board(int *original, int *copy) {
	int i;
	for (i = 0; i < BOARDSIZE; i++) {
		copy[i] = original[i];
	}
} 

/**
 *   Evaluation on the amount of disks
 *   ----------------------------------
 */
int eval_parity() {
	int max_val, min_val;

	max_val = count(max_colour, board);
	min_val = count(opponent(max_colour, NULL), board);
	
	if (min_val == 0) return 10000;
	return 100 * (max_val - min_val) / (max_val + min_val);
}

/**
 *   Evaluation on the amount of available moves
 *   --------------------------------------------
 */
int eval_mobility() {
	int max_val, min_val;
	int *moves = (int *) calloc(LEGALMOVSBUFSIZE, sizeof(int));

	legal_moves(max_colour, moves, NULL);
	max_val = moves[0];
	legal_moves(opponent(max_colour, NULL), moves, NULL);
	min_val = moves[0];

	if (max_val + min_val == 0) return 0;
	else return 100 * (max_val - min_val) / (max_val + min_val);
}

/**
 *   Evaluation on corners owned
 *   ----------------------------------
 */
int eval_corners() {
	int max_val = 0, min_val = 0;

	if (board[11] == max_colour) max_val++;
	if (board[18] == max_colour) max_val++;
	if (board[81] == max_colour) max_val++;
	if (board[88] == max_colour) max_val++;
	if (board[11] == opponent(max_colour, NULL)) min_val++;
	if (board[18] == opponent(max_colour, NULL)) min_val++;
	if (board[81] == opponent(max_colour, NULL)) min_val++;
	if (board[88] == opponent(max_colour, NULL)) min_val++;
	
	if (max_val + min_val == 0) return 0;
	else return 100 * (max_val - min_val) / (max_val + min_val);
}

/**
 *   Evaluation on the stability of disks
 *   -------------------------------------
 *   - The board is traversed in a spiral so that if a loop is found
 *     with all disks unstable, we can stop searching as all other disks
 *     will be unstable 
 */
int eval_stability() {
	int value, i, loc, flag;
	int unstable_loop = TRUE;
	int max_val = 0, min_val = 0;
	int *stability_board = (int *) calloc(BOARDSIZE, sizeof(int));

	for (i = 0; i < 64; i++) {
		// Check for timeout message
		MPI_Iprobe(0, TIMEOUT_TAG, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE); 
		if (flag) {
			MPI_Recv(&timeout, 1, MPI_INT, 0, TIMEOUT_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE); 
			free(stability_board);
			return -100000;
		}
		
		loc = spiral[i]; // Iterate in a spiral pattern

		if (board[loc] == EMPTY) continue;

		value = 0;

		// is horizontal border
		if (loc % 10 == 1 || loc % 10 == 8) value += H_BORDER;
		else if ((board[loc] == board[loc-1] && IS_H_BORDER(stability_board[loc-1])) ||
				 (board[loc] == board[loc+1] && IS_H_BORDER(stability_board[loc+1]))) {
			value += H_BORDER;
		}
		// is vertical border
		if (loc / 10 == 1 || loc / 10 == 8) value += V_BORDER;
		else if	((board[loc] == board[loc-10] && IS_V_BORDER(stability_board[loc-10])) ||
				 (board[loc] == board[loc+10] && IS_V_BORDER(stability_board[loc+10]))) {
			value += V_BORDER;
		}
		// is up down diagonal border
		if (loc % 10 == 1 || loc % 10 == 8 || loc / 10 == 1 || loc / 10 == 8) value += UDD_BORDER;
		else if	((board[loc] == board[loc-11] && IS_UDD_BORDER(stability_board[loc-11])) ||
				 (board[loc] == board[loc+11] && IS_UDD_BORDER(stability_board[loc+11]))) {
			value += UDD_BORDER;
		}
		// is down up diagonal border
		if (loc % 10 == 1 || loc % 10 == 8 || loc / 10 == 1 || loc / 10 == 8) value += DUD_BORDER;
		else if	((board[loc] == board[loc-9] && IS_DUD_BORDER(stability_board[loc-9])) ||
				(board[loc] == board[loc+9] && IS_DUD_BORDER(stability_board[loc+9]))) {
			value += DUD_BORDER;
		}
		// add evaluation
		if (board[loc] == max_colour) {
			if (IS_STABLE(value)) max_val++;
			else if (IS_UNSTABLE(value)) max_val--;
		} else {
			if (IS_STABLE(value)) min_val++;
			else if (IS_UNSTABLE(value)) min_val--;
		}	
		// update loc stability
		stability_board[loc] = value; 

		// Check if loop is unstable
		if (IS_H_BORDER(value) || IS_V_BORDER(value) || IS_UDD_BORDER(value) || IS_DUD_BORDER(value)) {
			unstable_loop = FALSE;
		}
		// if a loop contains no stable/semi-stable disks, there won't be any inside this loop
		if (IS_LOOP_COMPLETED(loc) && unstable_loop) break;
		else unstable_loop = TRUE;
	}
	free(stability_board);
	if (max_val + min_val == 0) return 0;
	return 100 * (max_val - min_val) / (max_val + min_val);
}

/**
 *   Weighting of evaluation functions
 *   ----------------------------------
 *   Called to get evalution for a position.
 *   - Without stability, depth can be pushed to about 9
 */
int eval_position() {
	int parity = 0, mobility = 0, corners = 0, stability = 0;
	int moves = 0;

	moves = count(max_colour, board) + count(opponent(max_colour, NULL), board);

	if (moves < 14) {
		parity = 5*eval_parity();
		corners = 30*eval_corners();
		mobility = 10*eval_mobility();
		stability = 20*eval_stability();
	}	else if (moves < 64 - STARTING_MAX_DEPTH) {
		parity = 25*eval_parity();
		corners = 30*eval_corners();
		mobility = eval_mobility();
		stability = 25*eval_stability();
	} else {
		parity = eval_parity();
	}
	
	return parity + corners + mobility + stability;
}

/**
 *   Rank i (i != 0) executes this code 
 *   ----------------------------------
 *   Called to get evalution for a move.
 *   - Minimax with alpha beta pruning
 */
int minimax(int current_colour, int depth, int alpha, int beta) {
	int *moves = (int *) calloc(LEGALMOVSBUFSIZE, sizeof(int));
	int *board_copy = (int *) calloc(BOARDSIZE, sizeof(int));
	int eval, max_eval, min_eval;
	int i;
	int flag;

	// Check for timeout message
	MPI_Iprobe(0, TIMEOUT_TAG, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE); 
	if (flag) {
		MPI_Recv(&timeout, 1, MPI_INT, 0, TIMEOUT_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE); 
	}
	if (timeout) {
		free(moves);
		free(board_copy);
		return -100000;
	}

	legal_moves(current_colour, moves, NULL);
	if (depth == 0 || moves[0] == 0) {
		free(moves);
		free(board_copy);
		return eval_position();
	}

	copy_board(board, board_copy);

	if (current_colour == max_colour) {
		max_eval = -1000000;
		for (i = 1; i <= moves[0]; i++) {
			make_move(moves[i], current_colour, NULL);
			eval = minimax(opponent(current_colour, NULL), depth-1, alpha, beta);
			copy_board(board_copy, board);
			if (eval > max_eval) max_eval = eval;
			if (max_eval > alpha) alpha = max_eval;
			if (beta <= alpha) break;
		}
		free(moves);
		free(board_copy);
		return max_eval;
	} else {
		min_eval = 1000000;
		for (i = 1; i <= moves[0]; i++) {
			make_move(moves[i], current_colour, NULL);
			eval = minimax(opponent(current_colour, NULL), depth-1, alpha, beta);
			copy_board(board_copy, board);
			if (eval < min_eval) min_eval = eval;
			if (min_eval < beta) beta = min_eval;
			if (beta <= alpha) break;
		}
		free(moves);
		free(board_copy);
		return min_eval;
	}
}