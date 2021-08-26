/* number of times a mature card can be failed */
/* before it is excluded from reviews. must be <= 255 */
#define MAX_LEECHES 4

/* the minimum age in seconds for card to be considerd a leech */
/* 3 days for example is 3 * 24 * 3600 (3d * 24h/d * 3600s/h) */
#define LEECH_AGE (5 * 24 * 3600)

/* minimum interval in seconds for grow by */
#define MINIMUM_INCREASE (24 * 3600)

/* amount to mutliply the cards interval by when it passes */
/* should be > 1. this leads to exponential growth */
#define GROWTH_RATE (1.5)

/* amount to mutliply the cards interval by when it fails */
/* should be < 1. this leads to exponential decay */
#define SHRINK_RATE (0.66)

/* card id formatting. needs to use ld */
#define CARDID "%05ld"

/* string used as a field delimiter */
/* don't set this to a string that could appear in fields */
#define DELIM "\t"

/* format for times in the output deck file */
const char *timefmt = "%Y年%m月%d日%H時%M分";
