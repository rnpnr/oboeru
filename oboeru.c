/* See LICENSE for license details. */
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
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

enum {
	CARD_PASS,
	CARD_FAIL
};

typedef struct {
	int64_t reviewed, due;
	size_t id, deck;
	char *extra;
	uint8_t leeches, nobump;
} Card;

typedef struct node {
	Card *card;
	struct node *next;
} Node;

static const char *scanfmt = "%ld" DELIM "%[^"DELIM"]" DELIM "%[^"DELIM"]" DELIM "%d" DELIM "%[^\n]";
static const char *logfmt = CARDID DELIM "%s" DELIM "%s" DELIM "%d" DELIM "%s\n";

static Node *head;
static size_t n_reviews, n_reviewed;

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
sighandler(const int signo)
{
	fprintf(stderr, remfmt, n_reviews - n_reviewed);
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
cleanup(Node *node, void *decks, void *reviews)
{
	freenodes(node);
	free(decks);
	free(reviews);
}

/* returns a filled out Card * after parsing */
static Card *
parse_line(const char *line)
{
	struct tm tm;
	char reviewed[BUF_SIZE], due[BUF_SIZE];
	Card *c = xmalloc(sizeof(Card));
	c->extra = xmalloc(BUF_SIZE);

	sscanf(line, scanfmt, &c->id, reviewed, due, &c->leeches, c->extra);

	memset(&tm, 0, sizeof(tm));
	strptime(reviewed, timefmt, &tm);
	c->reviewed = timegm(&tm);

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

static Card **
add_review(Card *r[], Card *c)
{
	r = xreallocarray(r, ++n_reviews, sizeof(Card **));
	r[n_reviews - 1] = c;

	return r;
}

static Card **
mkreviews(Node *node)
{
	Card **r = NULL;

	for (; node; node = node->next)
		if (node->card && needs_review(node->card))
			r = add_review(r, node->card);

	return r;
}

static int8_t
bump_card(Card *card, int8_t status)
{
	int64_t diff;
	time_t t = time(NULL);

	if (card->nobump && status != CARD_FAIL)
		return 0;

	diff = card->due - card->reviewed;
	if (diff < 0)
		fprintf(stderr, "card id: %ld: malformed review time\n", card->id);

	/* only hit this on the first time through */
	if (!card->nobump)
		card->reviewed = t;

	switch (status) {
	case CARD_PASS:
		/* - 1s to avoid rounding error */
		if (diff < MINIMUM_INCREASE - 1) {
			card->due = t + MINIMUM_INCREASE;
			return 1;
		}
		card->due = t + diff * GROWTH_RATE;
		break;
	case CARD_FAIL:
		if (diff > LEECH_AGE && !card->nobump)
			card->leeches++;

		if (diff * SHRINK_RATE < MINIMUM_INCREASE - 1)
			card->due = t + MINIMUM_INCREASE;
		else
			card->due = t + diff * SHRINK_RATE;
		return 1;
	}

	return 0;
}

static void
shuffle_reviews(Card *r[], size_t n)
{
	size_t i, j;
	Card *t;

	if (n <= 1)
		return;

	srand(time(NULL));

	/* this could be improved */
	for (i = 0; i < n; i++) {
		j = i + rand() % (n - i);

		t = r[j];
		r[j] = r[i];
		r[i] = t;
	}
}

static Card **
review_loop(Card *r[], const char *decks[], const char *fifo)
{
	char reply[BUF_SIZE];
	int fd;
	size_t i, j, n;
	struct timespec wait = { .tv_sec = 0, .tv_nsec = 50e6 };
	struct { const char *str; int status; } reply_map[] = {
		{ .str = "pass", .status = CARD_PASS },
		{ .str = "fail", .status = CARD_FAIL }
	};

	for (i = 0; i < n_reviews; i++, n_reviewed++) {
		fprintf(stdout, "%s\t"CARDID"\n", decks[r[i]->deck], r[i]->id);
		/* force a flush before blocking in open() */
		fflush(stdout);

		reply[0] = 0;
		fd = open(fifo, O_RDONLY);
		if (fd == -1)
			break;
		n = read(fd, reply, sizeof(reply));
		close(fd);

		/* strip a trailing newline */
		if (reply[n-1] == '\n')
			reply[n-1] = 0;

		if (!strcmp(reply, "quit"))
			break;

		for (j = 0; j < LEN(reply_map); j++)
			if (!strcmp(reply, reply_map[j].str))
				r[i]->nobump = bump_card(r[i], reply_map[j].status);

		/* if the card wasn't bumped it needs an extra review */
		if (r[i]->nobump) {
			r = add_review(r, r[i]);
			/* r[i+1] exists because we have added a review */
			shuffle_reviews(&r[i + 1], n_reviews - i - 1);
		}

		/* give the writing process time to close its fd */
		nanosleep(&wait, NULL);
	}
	return r;
}

static void
write_deck(const char *deck, size_t deck_id)
{
	FILE *fp;
	Node *node;
	Card *c;
	char reviewed[BUF_SIZE], due[BUF_SIZE];
	char path[PATH_MAX];

	if (dflag) {
		snprintf(path, sizeof(path), "%s.debug", deck);
		fp = fopen(path, "w+");
	} else {
		fp = fopen(deck, "w");
	}

	if (!fp)
		return;

	for (node = head; node; node = node->next) {
		c = node->card;
		if (!c || (c->deck != deck_id))
			continue;

		strftime(reviewed, sizeof(reviewed), timefmt,
			gmtime((time_t *)&c->reviewed));
		strftime(due, sizeof(due), timefmt,
			gmtime((time_t *)&c->due));

		fprintf(fp, logfmt, c->id, reviewed, due,
			c->leeches, c->extra);
	}
	fclose(fp);
}


int
main(int argc, char *argv[])
{
	Node *tail;
	Card **reviews;
	size_t i, n_decks = 0;
	const char *fifo = NULL, **decks = NULL;
	struct sigaction sa;
	struct stat sb;

	ARGBEGIN {
	case 'c':
		cflag = 1;
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

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = sighandler;
	sigaction(SIGUSR1, &sa, NULL);

	tail = head = xmalloc(sizeof(Node));

	/* remaining argv elements are deck files */
	for (i = 0; argc && *argv; argv++, i++, argc--) {
		decks = xreallocarray(decks, ++n_decks, sizeof(char *));
		decks[i] = *argv;

		tail = parse_file(*argv, tail, i);
	}

	reviews = mkreviews(head);

	if (cflag) {
		cleanup(head, decks, reviews);
		printf("Cards Due: %ld\n", n_reviews);
		return 0;
	}

	if (reviews == NULL) {
		cleanup(head, decks, reviews);
		die("mkreviews()\n");
	}

	shuffle_reviews(reviews, n_reviews);
	reviews = review_loop(reviews, decks, fifo);

	/* write updated data into deck files */
	for (i = 0; i < n_decks; i++)
		write_deck(decks[i], i);

	cleanup(head, decks, reviews);

	return 0;
}
