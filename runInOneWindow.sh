if [ -z "$1" ] || [ -z "$2" ]|| [ -z "$3" ]|| [ -z "$4" ]; then
echo "Usage: ./run.sh player1 player2 int_val_time_out_in_seconds num_processes"
#exit
else
echo "Piping defaults to game.json"
echo "{
	\"numPlayers\": 2,
	\"threads\": $4,
	\"time\": $3,
	\"path1\": \"$1\",
	\"path2\": \"$2\"
}" > game.json
fi

#*Stops gameserver if it is already running
pids="$(lsof -t -i:61235)"
if [ ! -z "$pids" ]; then 
	kill "$pids"
fi
sleep 2

java -jar IngeniousFramework.jar server -port 61235 &
sleep 2

java -jar IngeniousFramework.jar create -config "game.json" -game "OthelloReferee" -lobby "mylobby" -hostname localhost -port 61235

java -jar 'IngeniousFramework.jar' client -username foo -engine za.ac.sun.cs.ingenious.games.othello.engines.OthelloMPIEngine -game OthelloReferee -hostname localhost -port 61235 &  

java -jar 'IngeniousFramework.jar' client -username bar -engine za.ac.sun.cs.ingenious.games.othello.engines.OthelloMPIEngine -game OthelloReferee -hostname localhost -port 61235
