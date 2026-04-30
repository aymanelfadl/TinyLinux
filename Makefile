NAME = minibox

CC = gcc
CFLAGS = -Wall -Wextra -Werror
CPPFLAGS = -Iinclude

SRC_DIR = src
OBJ_DIR = obj

SRCS = \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/parser.c \
	$(SRC_DIR)/sandbox.c \
	$(SRC_DIR)/cgroups.c 

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(NAME)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

run: all
	sudo ./$(NAME) --hostname box1 --mem 100M --cpu 50 --pids 20 -- /bin/bash

.PHONY: all clean fclean re run