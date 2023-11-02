package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"webui/demic"

	"github.com/mkch/gg"
)

func main() {
	f := gg.Must(os.OpenFile("WebUI-log.txt", os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0777))
	defer f.Close()
	log.SetOutput(f)
	log.SetFlags(log.LstdFlags | log.Lshortfile)

	flag.Parse()
	if flag.NArg() < 1 || flag.Arg(0) != "DeMicPlugin" {
		log.Println("No arg")
		return
	}
	demic.WriteHello()

	StartServer(":8000")
	demic.OnMessage = onDemicMessage
	demic.OnInitMenuPopup = onDemicInitMenuPopup
	go demic.Serve()

	ok, ret := demic.CreateRootMenuItem(demic.Message{
		"ID": 600, "String": "WebUI*",
		"Submenu": []demic.Message{
			{"ID": 666, "String": "Item标题"},
			nil,
			{"ID": 667,
				"String": "submenu",
				"Submenu": []demic.Message{
					{"ID": 668, "String": "123"},
					{"ID": 669, "String": "456"},
				},
			},
		},
	})
	if !ok {
		log.Panic("CreateRootMenuItem failed")
	}
	var menu uintptr
	for _, item := range ret {
		if item.ID == 667 {
			menu = item.Handle
		}
	}
	ok = demic.RegisterInitMenuPopupListener([]uintptr{menu})
	if !ok {
		log.Panic("RegisterInitMenuPopupListener failed")
	}
	<-demic.Done()
}

func onDemicInitMenuPopup(menu uintptr) []demic.Message {
	muted, _ := demic.Muted()
	return []demic.Message{
		{"String": "A"},
		{"String": "B"},
		{"String": fmt.Sprintf("Muted: %v", muted)},
	}
}
