/* See LICENSE for license details. */
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "arg.h"
#include "util.h"
#include "config.h"

#define BUF_SIZE BUFSIZ
#define DELIM "\t" /* fixed unless a better parser gets implemented */

enum {
	CARD_PASS,
	CARD_FAIL
};

typedef struct {
	size_t id, deck;
	int leeches;
	/* seconds since epoch */
	int64_t created, due;
	char *extra;
	int8_t nobump;
} Card;

typedef struct node {
	Card *card;
	struct node *next;
} Node;

static const char *scanfmt = "%ld" DELIM "%s" DELIM "%s" DELIM "%d" DELIM "%[^\n]";
static const char *logfmt = "%05ld" DELIM "%s" DELIM "%s" DELIM "%d" DELIM "%s\n";

static Node *head;
static Card **reviews;
static size_t n_reviews;
static const char **decks;
static size_t n_decks;

/* option parsing variables */
char *argv0;
static int cflag;
static int dflag;

static void
usage(void)
{
	die("usage: %s [-cd] pipe deck [deck1 ...]\n", argv0);
}

static void
freenodes(Node *node)
{
	if (node->next)
		freenodes(node->next);

	if (node->card) {
		free(node->card->extra);
		free(node->card);
	}
	free(node);
	node = NULL;
}

static void
cleanup(void)
{
	freenodes(head);
	free(reviews);
	reviews = NULL;
}

/* returns a filled out Card * after parsing */
static Card *
parse_line(const char *line)
{
	struct tm tm;
	char created[BUF_SIZE], due[BUF_SIZE];
	Card *c = xmalloc(sizeof(Card));
	c->extra = xmalloc(BUF_SIZE);

	sscanf(line, scanfmt, &c->id, created, due, &c->leeches, c->extra);

	memset(&tm, 0, sizeof(tm));
	strptime(created, timefmt, &tm);
	c->created = timegm(&tm);

	memset(&tm, 0, sizeof(tm));
	strptime(due, timefmt, &tm);
	c->due = timegm(&tm);

	c->nobump = 0;

	return c;
}

/* returns the latest allocated node with no card */
static Node *
parse_file(const char *file, Node *node, size_t deck_id)
{
	FILE *fp;
	char *line = NULL;
	size_t len = 0;

	fp = fopen(file, "r");
	if (!fp)
		die("fopen(%s)\n", file);

	for (; getline(&line, &len, fp) != -1; node = node->next = xmalloc(sizeof(Node))) {
		node->next = NULL;
		node->card = parse_line(line);
		if (node->card)
			node->card->deck = deck_id;
	}

	free(line);
	fclose(fp);

	return node;
}

static int
needs_review(Card *card)
{
	time_t t;

	if (card->leeches >= MAX_LEECHES)
		return 0;

	t = time(NULL);

	if (card->due > t)
		return 0;

	return 1;
}

static void
add_review(Card *c)
{
	Card **r = reviews;
	r = xreallocarray(r, ++n_reviews, sizeof(Card **));
	r[n_reviews - 1] = c;
	reviews = r;
}

static void
mkreviews(Node *node)
{
	for (; node; node = node->next)
		if (node->card && needs_review(node->card))
			add_review(node->card);
}

static void
bump_card(Card *card, int status)
{
	int64_t diff;

	if (card->nobump)
		return;

	diff = card->due - card->created;
	if (diff < 0)
		fprintf(stderr, "card id: %ld: malformed review time\n", card->id);

	switch (status) {
	case CARD_PASS:
		if (diff < MINIMUM_INCREASE) {
			card->due += MINIMUM_INCREASE;
			add_review(card);
			card->nobump = 1;
		} else
			card->due += diff * GROWTH_RATE;
		break;
	case CARD_FAIL:
		if (diff > LEECH_AGE)
			card->leeches++;
		card->due += diff * SHRINK_RATE;
		add_review(card);
		card->nobump = 1;
	}
}

static void
shuffle_reviews(void)
{
	size_t i, j;
	Card *t, **r = reviews;

	if (n_reviews <= 1)
		return;

	srand(time(NULL));

	/* this could be improved */
	for (i = 0; i < n_reviews; i++) {
		j = i + rand() % (n_reviews - i);

		t = r[j];
		r[j] = r[i];
		r[i] = t;
	}
}

static void
review_loop(const char *fifo)
{
	Card **r = reviews;
	char reply[BUF_SIZE];
	int fd;
	size_t i, j, n;
	struct timespec wait = { .tv_sec = 0, .tv_nsec = 50 * 10e6 };
	struct { const char *str; int status; } reply_map[] = {
		{ .str = "pass", .status = CARD_PASS },
		{ .str = "fail", .status = CARD_FAIL }
	};

	for (i = 0; i < n_reviews; i++) {
		fprintf(stdout, "%s\t%ld\n", decks[r[i]->deck], r[i]->id);

		reply[0] = 0;
		fd = open(fifo, O_RDONLY);
		if (fd == -1)
			return;
		n = read(fd, reply, sizeof(reply));
		close(fd);

		/* strip a trailing newline */
		if (reply[n-1] == '\n')
			reply[n-1] = 0;

		if (!strcmp(reply, "quit"))
			return;

		for (j = 0; j < LEN(reply_map); j++)
			if (!strcmp(reply, reply_map[j].str))
				bump_card(r[i], reply_map[j].status);

		/* give the writing process time to close its fd */
		nanosleep(&wait, NULL);

		/* reviews can change in bump card */
		r = reviews;
	}
}

static void
write_deck(size_t deck_id)
{
	FILE *fp;
	Node *node;
	Card *c;
	char created[BUF_SIZE], due[BUF_SIZE];
	char path[PATH_MAX];

	if (dflag) {
		snprintf(path, sizeof(path), "%s.debug", decks[deck_id]);
		fp = fopen(path, "w+");
	} else {
		fp = fopen(decks[deck_id], "w");
	}

	if (!fp)
		return;

	for (node = head; node; node = node->next) {
		c = node->card;
		if (!c || (c->deck != deck_id))
			continue;

		strftime(created, sizeof(created), timefmt,
			gmtime((time_t *)&c->created));
		strftime(due, sizeof(due), timefmt,
			gmtime((time_t *)&c->due));

		fprintf(fp, logfmt, c->id, created, due,
			c->leeches, c->extra);
	}
	fclose(fp);
}


int
main(int argc, char *argv[])
{
	Node *tail;
	size_t deck_id;
	const char *fifo = NULL;
	struct stat sb;

	ARGBEGIN {
	case 'c':
		cflag = 2;
		break;
	case 'd':
		dflag = 1;
		break;
	default:
		usage();
	} ARGEND

	if ((cflag && argc < 1) || (!cflag && argc < 2))
		usage();

	if (!cflag) {
		fifo = *argv++;
		argc--;

		stat(fifo, &sb);
		if (!S_ISFIFO(sb.st_mode))
			usage();
	}

	tail = head = xmalloc(sizeof(Node));

	/* remaining argv elements are deck jsons */
	for (deck_id = 0; argc && *argv; argv++, deck_id++, argc--) {
		decks = xreallocarray(decks, ++n_decks, sizeof(char *));
		decks[deck_id] = *argv;

		tail = parse_file(*argv, tail, deck_id);
	}

	mkreviews(head);
	if (cflag) {
		cleanup();
		die("Cards Due: %ld\n", n_reviews);
	}

	shuffle_reviews();
	review_loop(fifo);

	/* write updated data into deck files */
	for (deck_id = 0; deck_id < n_decks; deck_id++)
		write_deck(deck_id);

	cleanup();

	return 0;
}
