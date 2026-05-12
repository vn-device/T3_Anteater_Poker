# **Anteater Poker**

##### 

##### **Project Overview**

Anteater Poker is a networked, client-server card game developed in the C programming language.

The application facilitates remote gameplay among friends, utilizing TCP/IP sockets for

communication and GTK 3.0 for the graphical user interface.



This version extends standard Texas Hold'em rules by integrating UCI's mascot through custom

Anteater face cards and special action cards.



##### **System Requirements**

This software is designed for and verified with the following configurations:

* Operating System: Linux
* Compiler: GCC (C11 standard recommended)
* Libraries: GTK 3.0
* Build Tool: GNU Make



##### **Installation \& Build**

To ensure a clean build, follow the developer steps outlined below:

1. Extract the Source:
* gtar xvzf Poker\_V1.0\_src.tar.gz
* cd poker
2. Compile the Binaries (build the server, client, and poker bot simultaneously):
* make all
3. Run Unit Tests (verify individual modules):
* make test



##### **Execution**

The server must be initialized before clients can connect.

1. Launch the Server:
* ./bin/server \&
2. Launch the Client:
* ./bin/poker



##### **Team Information (Team T3)**

* Jonghyun Choi
* Vinh Nguyen
* Hamza Faisal
* Richard Nguyen
* William Kuang
* Kun Liang



This project is licensed under the MIT License - see the COPYRIGHT file for details

