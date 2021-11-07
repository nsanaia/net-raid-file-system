SERVER_INPUT_FILES = server.c
SERVER_OUTPUT_FILE = server_o

build:
	gcc -g $(SERVER_INPUT_FILES) -o $(SERVER_OUTPUT_FILE) -lssl -lcrypto

clean:
	rm ./$(SERVER_OUTPUT_FILE)
