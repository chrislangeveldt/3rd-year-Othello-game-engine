#!/bin/bash

#*Runs a player named bar 
#*------------------------
java -jar 'IngeniousFramework.jar' client -username bar -engine za.ac.sun.cs.ingenious.games.othello.engines.OthelloMPIEngine -game OthelloReferee -hostname localhost -port 61235
