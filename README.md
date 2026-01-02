# Clue 0.1

The classic board game [Clue](https://en.wikipedia.org/wiki/Cluedo), played by computers!

## What?

For years I have been fascinated by computers playing games against each other. Almost 11 years ago (wow!) I was browsing StackOverflow and found [this gem of a question](https://codegolf.stackexchange.com/questions/42924/image-battle-of-colours) which pitted algorithms against eachother to fill an image with as much of their color as possible. I love this question because the game is so visual for humans to watch and the submissions use some really clever techniques to break down the problem without blowing the CPU budget.

I also have a love for mathematical games, which I share with my father. Family game night would often turn into "what is the highest score hand in [Five Crowns](https://en.wikipedia.org/wiki/Five_Crowns_(card_game))?" "how many cards do you need to guarantee a [SET](https://en.wikipedia.org/wiki/Set_(card_game))?" and, naturally, "what is the best strategy for Clue?"

When the questions get too complicated, we talk about running simulations. Thus, this project. A testbed where bots can play Clue, so we can finally settle what the optimal Clue strategy is once and for all.

See `server/first_ever_game.txt` for the server output from first game that finished without some terrible malfunction.

## Building

On Linux (or Mac?), running `build.sh` from the root directory will build the project. My preferred compiler is Clang but I'm sure it'll work with GCC if you just find + replace in the build script. The server executable will be located at `server/server`. See `clients/README` for more information on clients.

Can't help you with Windows right now. It will probably work under MinGW or WSL.

## Usage

Running `server/server` will start the game server with the settings specified in `settings.txt`. Once the server is running, it will wait SERVER_LOBBY_WAIT_TIME seconds (default 10) for clients to connect. Randy (a dummy client) can be started with `clients/randy/randy [ip] [port]`.

The server is not at all bulletproof. I would not recommend running it continuously on an open port right now.

## Future

I have some things in mind for the future of this project. In rough order:
1. Rewrite the server in Rust or Java for security, portability, and maintainability reasons.
  - Also provide a header-only library to simplify bot development in C.
2. Add a graphical frontend so you can watch the game.
3. Put the server on the web with a leaderboard.
4. Add the board and rolling dice (I suspect this affects the optimal strategy considerably but frankly it isn't that interesting to me).

## Making your own bot

If you are interested in making a bot, the network protocol is specified in `server/README`. It will almost certainly change before 1.0. I am happy to accept PRs and if a protocol change breaks your bot I'll fix it :).