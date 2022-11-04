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

const int MAX_DEPTH = 8;

const int REQUEST_MOVE_TAG = 0;
const int SEND_MOVE_TAG = 1;
const int MOVES_DONE_TAG = 2;

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
	int running = 0;
	int my_colour, flag = 0, moves_done;
	int move, eval;
	int alpha;
	int move_evaluated = 1, move_not_evaluated = 0;
	int *best_buffer = (int *) calloc(2, sizeof(int));
	int *board_copy= (int *) calloc(BOARDSIZE, sizeof(int));
	int *moves = (int *) calloc(LEGALMOVSBUFSIZE, sizeof(int));


	// Broadcast colour
	MPI_Bcast(&my_colour, 1, MPI_INT, 0, MPI_COMM_WORLD);
	// Broadcast running
	MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD);

	while (running == 1) {
		// Broadcast board
		MPI_Bcast(board, BOARDSIZE, MPI_INT, 0, MPI_COMM_WORLD);

		legal_moves(my_colour, moves, NULL);
		
		copy_board(board, board_copy);
		moves_done = 0;
		best_buffer[0] = -1;		//max eval move
		best_buffer[1] = -1000000; // max eval
		alpha = -1000000;
		while (!moves_done) { // do until master sends message that moves are done
			MPI_Iprobe(0, SEND_MOVE_TAG, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE); // look for move
			if (flag) {
				MPI_Recv(&move, 1, MPI_INT, 0, SEND_MOVE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // receive move
				
				//evaluate move ----------------

				make_move(move, my_colour, NULL);
				eval = minimax(my_colour, opponent(my_colour, NULL), 1, -1000000, 1000000);
				copy_board(board_copy, board);
				if (eval > best_buffer[1]) {
					best_buffer[0] = move;
					best_buffer[1] = eval;
				}
				if (best_buffer[1] > alpha) alpha = best_buffer[1];

				//------------------------------

				MPI_Send(&move_evaluated, 1, MPI_INT, 0, REQUEST_MOVE_TAG, MPI_COMM_WORLD); // request move & notify that a move has been evaluated 
			} else {
				MPI_Send(&move_not_evaluated, 1, MPI_INT, 0, REQUEST_MOVE_TAG, MPI_COMM_WORLD);// request move & move not evaluated
			}
			MPI_Iprobe(0, MOVES_DONE_TAG, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE); // look for done message
			if (flag) {
				MPI_Recv(&moves_done, 1, MPI_INT, 0, MOVES_DONE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // moves are done
			}
		}        

		MPI_Gather(best_buffer, 2, MPI_INT, NULL, 1, MPI_INT, 0, MPI_COMM_WORLD); // send best move and eval

		// Broadcast running
		MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD); 
	}
	free(best_buffer);
	free(board_copy);
	free(moves);
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
	int n_workers, flag;
	int i = 1;
	int moves_evaluated = 0, move_evaluated = 0;
	MPI_Status status;
	int *bests_buffer;
	int *moves = (int *) calloc(LEGALMOVSBUFSIZE, sizeof(int));

	MPI_Comm_size(MPI_COMM_WORLD, &n_workers);
	bests_buffer = (int *) calloc(n_workers*2, sizeof(int));
	n_workers--; // one less than comm_sz


	legal_moves(my_colour, moves, fp);
	
	// This loop dynamicly allocates moves to processes
	while (moves_evaluated < moves[0]) { // exit only when all moves have been EVALUATED
		flag = 0;
		MPI_Iprobe(MPI_ANY_SOURCE, REQUEST_MOVE_TAG, MPI_COMM_WORLD, &flag, &status); // look for move request
		if (flag) {
			// move request & return 1 if move has been evaluated
			MPI_Recv(&move_evaluated, 1, MPI_INT, status.MPI_SOURCE, REQUEST_MOVE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			moves_evaluated += move_evaluated;
			if (i <= moves[0]) {
				MPI_Send(&moves[i], 1, MPI_INT, status.MPI_SOURCE, SEND_MOVE_TAG, MPI_COMM_WORLD); // send unevaluated move
				i++;
			}
		}
	}
	// let workers know that no moves left
	move_evaluated = 1;
	for (i = 1; i <= n_workers; i++) { 
		MPI_Send(&move_evaluated, 1, MPI_INT, i, MOVES_DONE_TAG, MPI_COMM_WORLD);
	}

	// gather the best move en score from each worker
	MPI_Gather(MPI_IN_PLACE, 1, MPI_INT, bests_buffer, 2, MPI_INT, 0, MPI_COMM_WORLD); 

	// get best move
	for (i = 1; i <= n_workers; i++) { //index 0 and 1 are for master process;
		if (bests_buffer[2*i] == -1) continue; 
		if (bests_buffer[2*i+1] > eval) {
			r = bests_buffer[2*i];
			eval = bests_buffer[2*i+1];
		}
	}

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
	legal_moves(max_colour, moves, NULL);
	min_val = moves[0];

	if (max_val + min_val) return 0;
	else return 100 * (max_val - min_val) / (max_val + min_val);
}

int eval_position(int max_colour) {
	int parity, mobility;

	// parity = eval_parity(max_colour);
	mobility = eval_mobility(max_colour);
	
	return mobility;
}

int minimax(int max_colour, int current_colour, int depth, int alpha, int beta) {
	int *moves = (int *) calloc(LEGALMOVSBUFSIZE, sizeof(int));
	int *board_copy = (int *) calloc(BOARDSIZE, sizeof(int));
	int eval, max_eval, min_eval;
	int i;

	legal_moves(current_colour, moves, NULL);
	if (depth == MAX_DEPTH || moves[0] == 0) {
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