# Clue 0.2

The classic board game [Clue](https://en.wikipedia.org/wiki/Cluedo), played by computers!

## What?

For years I have been fascinated by computers playing games against each other. Almost 11 years ago (wow!) I was browsing StackOverflow and found [this gem of a question](https://codegolf.stackexchange.com/questions/42924/image-battle-of-colours) which pitted algorithms against eachother to fill an image with as much of their color as possible. I love this question because the game is so visual for humans to watch and the submissions use some really clever techniques to break down the problem without blowing the CPU budget.

I also have a love for mathematical games, which I share with my father. Family game night would often turn into "what is the highest score hand in [Five Crowns](https://en.wikipedia.org/wiki/Five_Crowns_(card_game))?" "how many cards do you need to guarantee a [SET](https://en.wikipedia.org/wiki/Set_(card_game))?" and, naturally, "what is the best strategy for Clue?"

When the questions get too complicated, we talk about running simulations. Thus, this project. A testbed where bots can play Clue, so we can finally settle what the optimal Clue strategy is once and for all.

## Building

From bash, running `build.sh` from the root directory will build the entire project, which includes the server and all clients.

The server can be built with `server/build.sh`. If you look inside you will see it is just `cargo build`. I have only tested on x86 Linux but I have no reason to suspect it would not work out of the box on any platform.

The clients each have their own build script in their directory. Right now there is only one client and it is using `clang` which is my preferred C compiler. I suspect `gcc` will work but there is a requirement for `unistd.h` so it is not going to work on Windows (stay tuned...)

## Usage

Running `server/server [settings file]` will start the game server with the settings specified in `[settings file]`. Once the server is running, it will wait for clients to connect. After at least 2 clients have connected, a game will begin after some number of seconds or after the maximum number of players have connected. Once a game begins, it will run in the background and the server will start a new lobby for the next game.

An example client can be started with `clients/example/example [ip] [port]`.

## Future

I have some things in mind for the future of this project. In rough order:
1. Add a graphical frontend so you can watch the game visually.
2. Put the server on the web with a leaderboard.
3. Make the C header library work on Windows.
4. Add the board and rolling dice (I suspect this affects the optimal strategy considerably but frankly it isn't that interesting to me).

## Making your own bot

If you are interested in making a bot, there's some hints in `server/README`. The bot in `clients/example/example.c` uses the `clue.h` header for most of the internals so the main program only has game logic.