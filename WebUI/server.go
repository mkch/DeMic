package main

import (
	"embed"
	"errors"
	"html/template"
	"io/fs"
	"log"
	"net"
	"net/http"
	"sync"
	"time"
	"webui/demic"
	"webui/modfs"

	"github.com/mkch/gg"
	"golang.org/x/net/websocket"
)

//go:embed res
var resFS embed.FS

var tpl = template.Must(template.ParseFS(gg.Must(fs.Sub(resFS, "res/templates")), "*"))
var staticFileServer = http.FileServer(http.FS(
	// Add a valid ModTime to embed.FS, so the response can be cached by client.
	// The original ModTime of the file returned by embed.FS.Open is 0.
	&modfs.FS{
		FS:           gg.Must(fs.Sub(resFS, "res/static")),
		LastModified: time.Now(),
	}))

var server http.Server

func StartServer(addr string) error {
	var tcpAddr *net.TCPAddr
	tcpAddr, err := net.ResolveTCPAddr("tcp", addr)
	if err != nil {
		return err
	}
	listener, err := net.ListenTCP("tcp", tcpAddr)
	if err != nil {
		return err
	}

	var mux http.ServeMux
	mux.HandleFunc("/", handleIndex)
	mux.Handle("/static/", http.StripPrefix("/static/", staticFileServer))
	mux.Handle("/DeMic", websocket.Handler(handleDeMicWS))
	server.Handler = &mux
	go server.Serve(listener)
	return nil
}

func handleIndex(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.Error(w, http.StatusText(http.StatusNotFound), http.StatusNotFound)
		return
	}
	tpl.ExecuteTemplate(w, "index.html", nil)
}

type wsNotification struct {
	Ch   chan<- map[string]any
	Done <-chan struct{}
}

var wsNotifications map[*websocket.Conn]wsNotification = make(map[*websocket.Conn]wsNotification)
var wsNotificationsL sync.RWMutex

func handleDeMicWS(conn *websocket.Conn) {
	password := conn.Request().FormValue("password")
	if password != "123" {
		websocket.JSON.Send(conn, map[string]any{"Type": "loginFailed"})
		return
	}
	if err := sendWs(conn, map[string]any{"Type": "loginOK"}); err != nil {
		log.Panic(err)
	}
	if !demic.RegisterStateListener(true) {
		return
	}
	muted, ok := demic.Muted()
	if !ok {
		return
	}
	if err := sendWs(conn, map[string]any{
		"Type":  "call",
		"Func":  "OnMicMuteStateChanged",
		"Param": muted,
	}); err != nil {
		log.Panic(err)
	}

	fromDeMic := make(chan map[string]any)
	wsNotificationsL.Lock()
	wsNotifications[conn] = wsNotification{Ch: fromDeMic, Done: conn.Request().Context().Done()}
	wsNotificationsL.Unlock()

	fromClient := make(chan map[string]any)
	go func() {
		defer func() {
			close(fromClient)
		}()
		for {
			var msg map[string]any
			if err := websocket.JSON.Receive(conn, &msg); err != nil {
				var netErr net.Error
				if !errors.As(err, &netErr) {
					log.Panic(err)
				}
				return
			}
			fromClient <- msg
		}
	}()

loop:
	for {
		var msg map[string]any
		var ok bool
		select {
		case msg, ok = <-fromClient:
			if !ok {
				break loop
			}
			if err := processWsIncoming(conn, msg); err != nil {
				log.Panic(err)
			}
		case msg = <-fromDeMic:
			if err := sendWs(conn, msg); err != nil {
				log.Panic(err)
			}
		}
	}

	wsNotificationsL.Lock()
	delete(wsNotifications, conn)
	wsNotificationsL.Unlock()
}

func onDemicMessage(msg demic.Message) {
	notifyWs(msg)
}

func notifyWs(msg map[string]any) {
	delete(msg, "ID")
	delete(msg, "Call")
	wsNotificationsL.RLock()
	defer wsNotificationsL.RUnlock()
	for _, n := range wsNotifications {
		select {
		case n.Ch <- msg:
		case <-n.Done:
			return
		}
	}
}

func processWsIncoming(conn *websocket.Conn, msg map[string]any) error {

	return nil
}

func sendWs(conn *websocket.Conn, msg any) error {
	if err := websocket.JSON.Send(conn, msg); err != nil {
		var netErr net.Error
		if !errors.As(err, &netErr) {
			return err
		}
	}
	return nil
}
