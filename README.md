# oboeru - 覚える
A collection of simple tools for reviewing flashcards.

Inspired by [oboeta](https://github.com/jtvaughan/oboeta).

## Just use Anki
[Anki](https://github.com/ankitects/anki) was always a little bit
excessive but it has only gotten worse as time goes on. Here are some
key problems with it:

* Cards are stored in a binary database.

* Relies on tons of terrible software such as qtwebengine.

* Build system relies on java despite there not being a single line of
  java in Anki's code base.

* Newer versions are not portable because of the terrible build system.

* Anki's buttons for rating cards can have unintended
  [consequences](https://web.archive.org/web/20201101024335if_/https://massimmersionapproach.com/table-of-contents/anki/low-key-anki/the-ease-factor-problem).

There is really no reason for flashcards to be this complicated.

## Solution
oboeru solves this problem by not caring about how you store your
cards. In fact all it does is maintain a ledger of card ids, their
creation date, their due date, and the number of times the card has
acted as a leech. Everything else is completely up to you.

### What _does_ oboeru do then?
oboeru reads supplied ledgers, finds cards up for review, shuffles
them, and finally prints out the originating ledgers name and card id
separated by a tab to stdout. Then it waits for commands provided via
a named pipe. Depending on the response it will update the current card
and print the next card to stdout or rewrite the ledgers and exit.

## Algorithm
oboeru uses an algorithm inspired by the simplicity of the
[Leitner](https://en.wikipedia.org/wiki/Leitner_system)
system but still drawing on some of the lessons of the SuperMemo
[SM-2](https://www.supermemo.com/en/archives1990-2015/english/ol/sm2)
algorithm. Specifically a card can only be passed or failed and it's
interval will always be changed by a fixed growth or shrink rate. Like
in Anki, cards can become leeches from too many failures. The relevant
code is as follows (some parts omitted):

	static void
	bump_card(Card *card, int status)
	{
		diff = card->due - card->reviewed;
		switch (status) {
		case CARD_PASS:
			if (diff < MINIMUM_INCREASE)
				card->due += MINIMUM_INCREASE;
			else
				card->due += diff * GROWTH_RATE;
			break;
		case CARD_FAIL:
			if (diff > LEECH_AGE)
				card->leeches++;
			card->due += diff * SHRINK_RATE;
		}
		card->reviewed = time(NULL);
	}

Take for example a card with an interval of 6 days, `GROWTH_RATE = 1.8`,
and `SHRINK_RATE = 0.66` (simplified for readability):

	bump_card(card, CARD_PASS)
		card->due = +10.8 days
	bump_card(card, CARD_FAIL)
		card->due = +4 days

## Ledger Format
The format for storing the ledgers is 1 card per line with fields tab
separated. An example follows:

	# id	reviewed		due			leeches	extra
	23	2021年07月09日16時18分	2021年08月09日05時01分	3	全て
	0099	2021年07月09日14時18分	2021年08月11日14時19分	0	飲み込む

The time formatting is configurable via `config.h`. The extra field
can be used for whatever, for example to check for duplicates. It can
contain any character besides `\0` and `\n`.

## Installation
Modify `config.h` and `config.mk` to your liking and use the following
to install (using root as needed):

	make clean install

## Example Usage
See [omoidasu](https://github.com/0x766F6964/omoidasu) for one example
of how to use these programs.
