CLIENT_INPUT_FILES = client.c parser.c cache.c
CLIENT_OUTPUT_FILE = client_o

build:
	gcc -g $(CLIENT_INPUT_FILES) -o $(CLIENT_OUTPUT_FILE) `pkg-config fuse --cflags --libs`

clean:
	rm ./$(CLIENT_OUTPUT_FILE)
