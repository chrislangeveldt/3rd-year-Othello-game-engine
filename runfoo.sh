#!/bin/bash

#*Runs a player named foo
#*------------------------
java -jar 'IngeniousFramework.jar' client -username foo -engine za.ac.sun.cs.ingenious.games.othello.engines.OthelloMPIEngine -game OthelloReferee -hostname localhost -port 61235 &  
