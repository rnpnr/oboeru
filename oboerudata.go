package main
import (
	"bufio"
	"fmt"
	"os"
	"strings"
	"strconv"
	"time"
)

func usage() {
	fmt.Fprintf(os.Stderr, "usage: %s fifo\n", os.Args[0])
}

func makemap(deck string) map[int]string {
	f, err := os.OpenFile(deck + ".data", os.O_RDONLY, 0444)
	defer f.Close()
	if err != nil {
		return nil
	}

	m := make(map[int]string)
	r := bufio.NewScanner(f)
	for r.Scan() {
		str := strings.SplitN(r.Text(), "\t", 2)
		key, _ := strconv.Atoi(str[0])
		m[key] = str[1]
	}
	return m;
}

func wait_and_print(fifo string) {
	m := make(map[string]map[int]string)
	for {
		f, err := os.OpenFile(fifo, os.O_RDONLY, os.ModeNamedPipe)
		if err != nil {
			break
		}

		r := bufio.NewScanner(f)
		r.Scan()
		if r.Text() == "" {
			f.Close()
			break
		}

		var key int
		var deck string
		fmt.Sscanf(r.Text(), "%s\t%d", &deck, &key)

		_, ok := m[deck]
		if !ok {
			split := strings.Split(deck, ".")
			m[deck] = makemap(split[0])
		}

		str, _ := m[deck][key]

		fmt.Fprintln(os.Stdout, str)

		f.Close()
		time.Sleep(50 * time.Millisecond)
	}
}

func main() {
	if len(os.Args) != 2 {
		usage()
		os.Exit(1)
	}

	fifo := os.Args[1]

	wait_and_print(fifo)
}
