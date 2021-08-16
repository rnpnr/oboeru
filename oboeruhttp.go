package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"net/http"
	"os"
	"strings"
)

var ctx context.Context
var cancel context.CancelFunc

var front, back string
var cardlock = false

var html_midf, html_midb, html_quit string 
const html_head = "<!DOCTYPE html><html><head><meta charset='utf-8' />" +
		  "<link rel='stylesheet' type='text/css' href='/_/style.css'>" +
		  "<title>oboeru</title></head><body><div>"
const html_foot = "</div></body></html>"

func usage() {
	fmt.Fprintf(os.Stderr, "usage: %s [-p port] [-FPQS label]\n", os.Args[0])
	flag.PrintDefaults()
}

func create_req_map() map[string]string {
	return map[string]string {
		"/fail": "fail",
		"/pass": "pass",
		"/quit": "quit",
		"/show": "show",
	}
}

func format_page(head string, body string, mid string, foot string) string {
	return strings.Join([]string{head, body, mid, foot}, "")
}

func get_next_card() []string {
	stdin := bufio.NewScanner(os.Stdin)
	stdin.Scan()
	return strings.Split(stdin.Text(), "\t")
}

func show_card(w http.ResponseWriter, card string, mid string) {
	html := format_page(html_head, card, mid, html_foot)
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	fmt.Fprint(w, html)
}

func quit(w http.ResponseWriter) {
	show_card(w, html_quit, "")
	fmt.Fprintln(os.Stdout, "quit")
	cancel() /* send signal to shutdown srv */
}

func response(w http.ResponseWriter, r *http.Request) {
	req_map := create_req_map()

	/* won't err since all invalid reqs have been rerouted to "/" */
	str, _ := req_map[r.RequestURI]

	switch str {
	case "show":
		show_card(w, back, html_midb)
	case "quit":
		quit(w)
	case "fail":
		fallthrough
	case "pass":
		cardlock = false
		fmt.Fprintln(os.Stdout, str)
		http.Redirect(w, r, "/", http.StatusSeeOther)
	}
}

func serve(w http.ResponseWriter, r *http.Request) {
	var card []string
	if cardlock == false {
		card = get_next_card()
		if len(card) != 2 {
			quit(w)
		}

		cardlock = true
		front = card[0]
		back = card[1]
	}

	show_card(w, front, html_midf)
}

func main() {
	var (
		port = flag.Uint("p", 6969, "port number")
		fail = flag.String("F", "fail", "fail label")
		pass = flag.String("P", "pass", "pass label")
		show = flag.String("S", "show", "show label")
		quit = flag.String("Q", "quit", "quit label")
		bye = flag.String("q", "Goodbye!", "text to display before quitting")
	)

	flag.Usage = usage
	flag.Parse()

	html_midf = strings.Join([]string{"<div class='link'><a href='/quit'>",
				*quit, "</a> · <a href='/show'>", *show,
				"</a></div>"}, "")
	html_midb = strings.Join([]string{"<div class='link'><a href='/fail'>",
				*fail, "</a> · <a href='/quit'>", *quit,
				"</a> · <a href='/pass'>", *pass, "</a></div>"}, "")
	html_quit = *bye

	ctx, cancel = context.WithCancel(context.Background())

	wd, _ := os.Getwd()
	fs := http.FileServer(http.Dir(wd))

	http.HandleFunc("/", serve)
	
	/* pretend requested files are in wd/_/ so that they can
	 * be accessed on the fly */
	http.Handle("/_/", http.StripPrefix("/_/", fs))
	
	/* map allowed reqs to the handler fn response() */
	for k, _ := range create_req_map() {
		http.HandleFunc(k, response)
	}

	server := fmt.Sprintf(":%d", *port)
	srv := &http.Server{Addr: server}
	go func() {
		srv.ListenAndServe()
	}()
	<- ctx.Done() /* wait for signal to shutdown */

	srv.Shutdown(ctx)
}
