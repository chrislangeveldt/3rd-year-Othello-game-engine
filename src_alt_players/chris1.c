/* vim: :se ai :se sw=4 :se ts=4 :se sts :se et */

/*H**********************************************************************
 *
 *    This is a skeleton to guide development of Othello engines that can be used
 *    with the Ingenious Framework and a Tournament Engine. 
 *
 *    The communication with the referee is handled by an implementaiton of comms.h,
 *    All communication is performed at rank 0.
 *
 *    Board co-ordinates for moves start at the top left corner of the board i.e.
 *    if your engine wishes to place a piece at the top left corner, 
 *    the "gen_move_master" function must return "00".
 *
 *    The match is played by making alternating calls to each engine's 
 *    "gen_move_master" and "apply_opp_move" functions. 
 *    The progression of a match is as follows:
 *        1. Call gen_move_master for black player
 *        2. Call apply_opp_move for white player, providing the black player's move
 *        3. Call gen move for white player
 *        4. Call apply_opp_move for black player, providing the white player's move
 *        .
 *        .
 *        .
 *        N. A player makes the final move and "game_over" is called for both players
 *    
 *    IMPORTANT NOTE:
 *        Write any (debugging) output you would like to see to a file. 
 *        	- This can be done using file fp, and fprintf()
 *        	- Don't forget to flush the stream
 *        	- Write a method to make this easier
 *        In a multiprocessor version 
 *        	- each process should write debug info to its own file 
 *H***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <mpi.h>
#include <time.h>
#include <assert.h>
#include "comms.h"

const int STARTING_MAX_DEPTH = 7;
const int CUT_OFF_TIME = 4;

const int TRUE = 1;
const int FALSE = 0;

const int REQUEST_MOVE_TAG = 0;
const int SEND_MOVE_TAG = 1;
const int MOVES_DONE_TAG = 2;
const int SEND_ALPHA_TAG = 3;
const int GO_DEEPER_TAG = 4;
const int TIMEOUT_TAG = 5;

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
void log_msg(char *msg);
void copy_board(int *original, int *copy);
int minimax(int max_colour, int current_colour, int depth, int alpha, int beta);

int *board;
int *stability_board;
clock_t start, end;
int timeout;
int max_depth;

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

	while (running == 1) {
		/* Receive next command from referee */
		if (comms_get_cmd(cmd, opponent_move) == FAILURE) {
			char msg[100];
			sprintf(msg, "Error opponent move\n");
			log_msg(msg);
			fprintf(fp, "Error getting cmd\n");
			fflush(fp);
			running = 0;
			break;
		}

		/* Received game_over message */
		if (strcmp(cmd, "game_over") == 0) {
			char msg[100];
			sprintf(msg, "Game over\n");
			log_msg(msg);
			running = 0;
			fprintf(fp, "Game over\n");
			fflush(fp);
			break;

		/* Received gen_move message */
		} else if (strcmp(cmd, "gen_move") == 0) {
			char msg[100];
			sprintf(msg, "My turn from Ref\n");
			log_msg(msg);
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
			sprintf(msg, "Turn Done\n\n");
			log_msg(msg);

		/* Received opponent's move (play_move mesage) */
		} else if (strcmp(cmd, "play_move") == 0) {
			char msg[100];
			sprintf(msg, "Play opponents move\n");
			log_msg(msg);
			apply_opp_move(opponent_move, my_colour, fp);
			print_board(fp);

		/* Received unknown message */
		} else {
			char msg[100];
			sprintf(msg, "Unknown message\n");
			log_msg(msg);
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
	int running = 0;
	int my_colour, flag = 0, moves_done;
	int move, eval;
	int alpha, other_alpha;
	int i, comm_sz, my_rank;
	int *best_buffer = (int *) calloc(2, sizeof(int));
	int *board_copy= (int *) calloc(BOARDSIZE, sizeof(int));
	MPI_Request request;
	MPI_Status status;

	MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

	// Broadcast colour
	MPI_Bcast(&my_colour, 1, MPI_INT, 0, MPI_COMM_WORLD);
	// Broadcast running
	MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD);

	while (running == 1) {
		// Broadcast board
		MPI_Bcast(board, BOARDSIZE, MPI_INT, 0, MPI_COMM_WORLD);
		
		copy_board(board, board_copy);
		timeout = FALSE;
		max_depth = STARTING_MAX_DEPTH;
		while (!timeout) {

			char msg[100];
			sprintf(msg, "max depth: %d\n", max_depth);
			log_msg(msg);

			moves_done = FALSE;
			best_buffer[0] = -1;		//max eval move
			best_buffer[1] = -1000000; // max eval
			alpha = -1000000;
			while (!moves_done) { // do until master sends message that moves are done
				MPI_Iprobe(0, SEND_MOVE_TAG, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE); // look for move
				if (flag) {
					MPI_Recv(&move, 1, MPI_INT, 0, SEND_MOVE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // receive move
					
					//evaluate move ----------------

					make_move(move, my_colour, NULL);
					eval = minimax(my_colour, opponent(my_colour, NULL), 1, alpha, 1000000);
					copy_board(board_copy, board);
					if (eval > best_buffer[1]) {
						best_buffer[0] = move;
						best_buffer[1] = eval;
					}
					if (best_buffer[1] > alpha) {
						alpha = best_buffer[1];
						for (i = 1; i < comm_sz; i++) {
							if (i == my_rank) continue;
							MPI_Isend(&alpha, 1, MPI_INT, i, SEND_ALPHA_TAG, MPI_COMM_WORLD, &request);
						}
					}

					//------------------------------

					MPI_Isend(&TRUE, 1, MPI_INT, 0, REQUEST_MOVE_TAG, MPI_COMM_WORLD, &request); // request move & notify that a move has been evaluated 
				} else {
					MPI_Isend(&FALSE, 1, MPI_INT, 0, REQUEST_MOVE_TAG, MPI_COMM_WORLD, &request);// request move & move not evaluated
				}

				MPI_Iprobe(MPI_ANY_SOURCE, SEND_ALPHA_TAG, MPI_COMM_WORLD, &flag, &status);
				if (flag) {
					MPI_Recv(&other_alpha, 1, MPI_INT, status.MPI_SOURCE, SEND_ALPHA_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
					if (other_alpha > alpha) alpha = other_alpha;
				}

				MPI_Iprobe(0, TIMEOUT_TAG, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE); 
				if (flag) {
					char msg[200];
					sprintf(msg, "Timeout in runworker\n");
					log_msg(msg);
					MPI_Recv(&timeout, 1, MPI_INT, 0, TIMEOUT_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
					MPI_Recv(&moves_done, 1, MPI_INT, 0, MOVES_DONE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // moves are done 
				}

				MPI_Iprobe(0, MOVES_DONE_TAG, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE); // look for done message
				if (flag) {
					MPI_Recv(&moves_done, 1, MPI_INT, 0, MOVES_DONE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // moves are done
				}
			}        
			MPI_Gather(best_buffer, 2, MPI_INT, NULL, 1, MPI_INT, 0, MPI_COMM_WORLD); // send best move and eval
			max_depth++;
		}
		// Broadcast running
		MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD); 
		char msg[100];
		sprintf(msg, "Move done\n\n");
		log_msg(msg);
	}
	free(best_buffer);
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

int strategy(int my_colour, FILE *fp) {
	int r = -1, eval = -1000000;
	int comm_sz, flag;
	int i, j;
	int moves_evaluated, move_evaluated = 0;
	double time_spent = 0;
	MPI_Status status;
	MPI_Request request;
	int *bests_buffer;
	int *moves = (int *) calloc(LEGALMOVSBUFSIZE, sizeof(int));

	start = clock();

	MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);
	bests_buffer = (int *) calloc(comm_sz*2, sizeof(int));

	legal_moves(my_colour, moves, fp);

	char msg[100];
	sprintf(msg, "Number of moves: %d\n", moves[0]);
	log_msg(msg);
	
	timeout = FALSE;
	while (!timeout) {
		moves_evaluated = 0;
		i = 1;
		// This loop dynamicly allocates moves to processes
		while (moves_evaluated < moves[0]) { // exit only when all moves have been EVALUATED
			// flag = 0;
			MPI_Iprobe(MPI_ANY_SOURCE, REQUEST_MOVE_TAG, MPI_COMM_WORLD, &flag, &status); // look for move request
			if (flag) {
				// move request & return 1 if move has been evaluated
				MPI_Recv(&move_evaluated, 1, MPI_INT, status.MPI_SOURCE, REQUEST_MOVE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				moves_evaluated += move_evaluated;
				if (i <= moves[0]) {
					MPI_Isend(&moves[i], 1, MPI_INT, status.MPI_SOURCE, SEND_MOVE_TAG, MPI_COMM_WORLD, &request); // send unevaluated move
					i++;
				}
			}
			end = clock();
			time_spent = (double)(end - start) / CLOCKS_PER_SEC;
			if (time_spent > CUT_OFF_TIME - 0.5) {
				for (j = 1; j < comm_sz; j++) { 
					MPI_Isend(&TRUE, 1, MPI_INT, j, TIMEOUT_TAG, MPI_COMM_WORLD, &request);
				}
				sprintf(msg, "Timeout sent\n");
				log_msg(msg);
				moves_evaluated = moves[0];
				timeout = TRUE;
			}
		}
		// let workers know that no moves left
		for (i = 1; i < comm_sz; i++) { 
			MPI_Isend(&TRUE, 1, MPI_INT, i, MOVES_DONE_TAG, MPI_COMM_WORLD, &request);
		}

		sprintf(msg, "Before Gather\n");
		log_msg(msg);
		// gather the best move and score from each worker
		MPI_Gather(MPI_IN_PLACE, 1, MPI_INT, bests_buffer, 2, MPI_INT, 0, MPI_COMM_WORLD); 
		sprintf(msg, "After Gather\n");
		log_msg(msg);

		// get best move
		for (i = 1; i < comm_sz; i++) { //index 0 and 1 are for master process;
			if (bests_buffer[2*i] == -1) continue; 
			if (bests_buffer[2*i+1] > eval) {
				r = bests_buffer[2*i];
				eval = bests_buffer[2*i+1];
			}
		}

		// end = clock();
		// time_spent = (double)(end - start) / CLOCKS_PER_SEC;
		// if (time_spent > CUT_OFF_TIME - 0.5) timeout = TRUE;
	}

	sprintf(msg, "MOVE: %d\n", r);
	log_msg(msg);

	free(moves);
	free(bests_buffer);
	return(r);
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
	// print_board(fptr);
    fclose(fptr);
}

void copy_board(int *original, int *copy) {
	int i;
	for (i = 0; i < BOARDSIZE; i++) {
		copy[i] = original[i];
	}
} 

int eval_parity(int max_colour) {
	int max_val, min_val;

	max_val = count(max_colour, board);
	min_val = count(opponent(max_colour, NULL), board);
	
	return 100 * (max_val - min_val) / (max_val + min_val);
}

int eval_mobility(int max_colour) {
	int max_val, min_val;
	int *moves = (int *) calloc(LEGALMOVSBUFSIZE, sizeof(int));

	legal_moves(max_colour, moves, NULL);
	max_val = moves[0];
	legal_moves(opponent(max_colour, NULL), moves, NULL);
	min_val = moves[0];

	if (max_val + min_val == 0) return 0;
	else return 100 * (max_val - min_val) / (max_val + min_val);
}

int eval_corners(int max_colour) {
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

// int eval_stability(int max_colour) {
// 	int max_val, min_val;

// 	if (board[11] == EMPTY && board[18] == EMPTY && board[81] == EMPTY && board[88] == EMPTY) {
// 		return 0;
// 	}
	
// 	return 100 * (max_val - min_val) / (max_val + min_val);
// }

int eval_position(int max_colour) {
	int parity, mobility, corners;
	int moves = 0;

	moves = count(max_colour, board) + count(opponent(max_colour, NULL), board);

	if (moves <= 16) {
		parity = 5*eval_parity(max_colour);
		corners = 20*eval_corners(max_colour);
		mobility = 5*eval_mobility(max_colour);
	}	else {
		parity = 5*eval_parity(max_colour);
		corners = 20*eval_corners(max_colour);
		mobility = 0;
	}

	return parity + corners + mobility;
}

int minimax(int max_colour, int current_colour, int depth, int alpha, int beta) {
	int *moves = (int *) calloc(LEGALMOVSBUFSIZE, sizeof(int));
	int *board_copy = (int *) calloc(BOARDSIZE, sizeof(int));
	int eval, max_eval, min_eval;
	int i;
	int flag;

	MPI_Iprobe(0, TIMEOUT_TAG, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE); 
	if (flag) {
		char msg[200];
		sprintf(msg, "Timeout in minimax\n");
		log_msg(msg);
		MPI_Recv(&timeout, 1, MPI_INT, 0, TIMEOUT_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE); 
	}
	if (timeout) {
		free(moves);
		free(board_copy);
		return -10000;
	}

	legal_moves(current_colour, moves, NULL);
	if (depth == max_depth || moves[0] == 0) {
		free(moves);
		free(board_copy);
		return eval_position(max_colour);
	}

	copy_board(board, board_copy);

	if (current_colour == max_colour) {
		max_eval = -1000000;
		for (i = 1; i <= moves[0]; i++) {
			make_move(moves[i], current_colour, NULL);
			eval = minimax(max_colour, opponent(current_colour, NULL), depth+1, alpha, beta);
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
			eval = minimax(max_colour, opponent(current_colour, NULL), depth+1, alpha, beta);
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