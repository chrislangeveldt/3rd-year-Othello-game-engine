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

#undef DEBUG

const int MAX_DEPTH = 5;

const int EMPTY = 0;
const int BLACK = 1;
const int WHITE = 2;

const int OUTER = 3;
const int ALLDIRECTIONS[8] = {-11, -10, -9, -1, 1, 9, 10, 11};
const int BOARDSIZE = 100;

const int LEGALMOVSBUFSIZE = 65;
const char piecenames[4] = {'.','b','w','?'};

const int eval_board[90] = {0,  0,  0,  0,  0,  0,  0,  0,  0, 0,
							0,  4, -3,  2,  2,  2,  2, -3,  4, 0,
							0, -3, -4, -1, -1, -1, -1, -4, -3, 0,
							0,  2, -1,  1,  0,  0,  1, -1,  2, 0,
							0,  2, -1,  0,  1,  1,  0, -1,  2, 0,
							0,  2, -1,  0,  1,  1,  0, -1,  2, 0,
							0,  2, -1,  1,  0,  0,  1, -1,  2, 0,
							0, -3, -4, -1, -1, -1, -1, -4, -3, 0,
							0,  4, -3,  2,  2,  2,  2, -3,  4, 0,};

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
int minimax(int max_colour, int my_colour, int depth, int alpha, int beta);
void log_msg(char *msg);

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
	int running;
	int my_colour;
	int temp_eval, best_eval;
	int comm_sz, rank;
	int i, j, best_move;
	int *buffer = calloc(2, sizeof(int));
	int *moves = calloc(LEGALMOVSBUFSIZE, sizeof(int));
	int *my_moves = calloc(LEGALMOVSBUFSIZE, sizeof(int));
	int *temp_board = calloc(BOARDSIZE, sizeof(int));

	MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	MPI_Bcast(&my_colour, 1, MPI_INT, 0, MPI_COMM_WORLD); // receive colour from master

	MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD); // receive running from master
	 
	while (running == 1) {
		MPI_Bcast(board, BOARDSIZE, MPI_INT, 0, MPI_COMM_WORLD); // receive board from master
      
		legal_moves(my_colour, moves, NULL);
		my_moves[0] = 0;
		for (i = rank, j = 1; i <= moves[0]; i+=(comm_sz - 1)) { // get this worker's moves
			my_moves[0]++;
			my_moves[j++] = moves[i];
		}
		
		best_eval = -1000000;
		best_move = -1;
		if (my_moves[0] != 0) {
			for (j = 0; j < BOARDSIZE; j++) { // store a copy of board
				temp_board[j] = board[j];
			}
			for (i = 1; i <= my_moves[0]; i++) { // getting eval from each move
				// Make move and get it's eval
				make_move(my_moves[i], my_colour, NULL);
				temp_eval = minimax(my_colour, opponent(my_colour, NULL), 1, -1000000, 1000000);
				// char msg[100];
				// sprintf(msg, "%c Move %d, Eval %d\n", nameof(my_colour), my_moves[i], temp_eval);
				// log_msg(msg);

				if (temp_eval > best_eval) {
					best_eval = temp_eval;
					best_move = my_moves[i];
				}
				for (j = 0; j < BOARDSIZE; j++) { // reset board
					board[j] = temp_board[j];
				}
			}
		}
		// char msg[100];
		// sprintf(msg, "%c Chosen Move %d\n\n", nameof(my_colour), best_move);
		// log_msg(msg);
		buffer[0] = best_move;
		buffer[1] = best_eval;
		MPI_Gather(buffer, 2, MPI_INT, NULL, 1, MPI_INT, 0, MPI_COMM_WORLD); // send buffer to master

		MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD); // receive running from master
	}
	free(buffer);
	free(moves);
	free(my_moves);
	free(temp_board);
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
	int r, eval, comm_sz, i;
	int *moves;

	MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);

	moves = calloc(comm_sz*2, sizeof(int));
	
	// Gather the best_value move and eval from workers 
	MPI_Gather(MPI_IN_PLACE, 1, MPI_INT, moves, 2, MPI_INT, 0, MPI_COMM_WORLD); 

	r = -1;
	eval = -1000000;
	// char msg[100];
	for (i = 2; i < comm_sz*2; i+=2) { //index 0 and 1 are for master process;
		if (moves[i] == -1) break; 
		if (moves[i+1] > eval) {
			r = moves[i];
			eval = moves[i+1];
		}
		// sprintf(msg, "%c Move %d, Eval %d\n",nameof(my_colour), moves[i], moves[i+1]);
		// log_msg(msg);
	}
	// sprintf(msg, "%c Chosen move %d\n\n", nameof(my_colour), r);
	// log_msg(msg);

	free(moves);
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

int eval_static(int max_colour) {
	int maxP_value = 0, minP_value = 0;
	int i;

	for (i = 11; i <= 88; i++) {
		if (i % 10 == 9) i += 2;
		if (board[i] == max_colour) {
			maxP_value += eval_board[i];
		} else if (board[i] == opponent(max_colour, NULL)) {
			minP_value += eval_board[i];
		}
	}
	return maxP_value - minP_value;
}

int eval_parity(int max_colour) {
	int maxP_value, minP_value;
	int parity;

	maxP_value = count(max_colour, board);
	minP_value = count(opponent(max_colour, NULL), board);
	parity = 100 * (maxP_value - minP_value) / (maxP_value + minP_value);

	return parity;
}

int eval_mobility(int max_colour) {
	int maxP_value, minP_value;
	int mobility = 0;
	int *moves = calloc(LEGALMOVSBUFSIZE, sizeof(int));

	legal_moves(max_colour, moves, NULL);
	maxP_value = moves[0];
	legal_moves(opponent(max_colour, NULL), moves, NULL);
	minP_value = moves[0];

	if (maxP_value + minP_value != 0) {
		mobility = 100 * (maxP_value - minP_value) / (maxP_value + minP_value);
	} 

	return mobility;
}

// int eval_stability(int max_colour) {
// 	int maxP_value, minP_value;
// 	int stability = 0;
// 	int *moves = calloc(LEGALMOVSBUFSIZE, sizeof(int));

// 	legal_moves(max_colour, moves, NULL);
// 	maxP_value = moves[0];
// 	legal_moves(opponent(max_colour, NULL), moves, NULL);
// 	minP_value = moves[0];

// 	if (maxP_value + minP_value != 0) {
// 		mobility = 100 * (maxP_value - minP_value) / (maxP_value + minP_value);
// 	} 

// 	return mobility;
// }

int eval(int max_colour) {
	int stat;
	int parity;
	int mobility;

	// stat = eval_static(max_colour);
	// parity = eval_parity(max_colour);
	mobility = eval_mobility(max_colour);

	// return parity;
	return mobility;
}

int minimax(int max_colour, int my_colour, int depth, int alpha, int beta) {
	int *moves = calloc(LEGALMOVSBUFSIZE, sizeof(int));
	int *temp_board = calloc(BOARDSIZE, sizeof(int));
	int i, j, value, best_value;

	for (j = 0; j < BOARDSIZE; j++) {
		temp_board[j] = board[j];
	}

	legal_moves(my_colour, moves, NULL);
	if (depth == MAX_DEPTH || moves[0] == 0) {
		free(temp_board);
		free(moves);
		return eval(max_colour); // max_colour???????
	}

	if (my_colour == max_colour) {
		best_value = -1000000;
		for (i = 1; i <= moves[0]; i++) {
			make_move(moves[i], my_colour, NULL);
			value = minimax(max_colour, opponent(my_colour, NULL), depth+1, alpha, beta);

			// char msg[100];
			// sprintf(msg, "Max\nDepth: %d\nEval: %d\n\n", depth, value);
			// log_msg(msg);

			for (j = 0; j < BOARDSIZE; j++) {
				board[j] = temp_board[j];
			}
			if (value > best_value) best_value = value;
			// if (best_value > alpha) alpha = best_value;
			// if (beta <= alpha) {
			// 	break;
			// }
		}
		free(temp_board);
		free(moves);
		return best_value;
	} else {
		best_value = 1000000;
		for (i = 1; i <= moves[0]; i++) {
			make_move(moves[i], my_colour, NULL);
			value = minimax(max_colour, opponent(my_colour, NULL), depth+1, alpha, beta);

			// char msg[100];
			// sprintf(msg, "Min\nDepth: %d\nEval: %d\n\n", depth, value);
			// log_msg(msg);

			for (j = 0; j < BOARDSIZE; j++) {
				board[j] = temp_board[j];
			}
			if (value < best_value) best_value = value;
			// if (best_value < beta) beta = best_value;
			// if (beta <= alpha) {
			// 	break;
			// }
		}
		free(temp_board);
		free(moves);
		return best_value;
	}
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