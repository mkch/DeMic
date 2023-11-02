package demic

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"errors"
	"io"
	"log"
	"os"
	"sync"
)

type Message = map[string]any

var OnMessage func(Message)
var OnUnload func()
var OnInitMenuPopup func(menu uintptr) []Message

func Serve() {
	go writeLoop()
	readLoop()
	close(done)
}

func ToggleMuted() bool {
	var msg = Message{
		"Type": "call",
		"Func": "ToggleMuted",
	}
	_, ok := write(msg, false)
	return ok
}

func Muted() (muted bool, ok bool) {
	var msg = Message{
		"Type": "call",
		"Func": "IsMuted",
	}
	var ret Message
	if ret, ok = write(msg, true); !ok {
		return false, false
	}
	return ret["Value"].(bool), true
}

func RegisterStateListener(reg bool) bool {
	var msg = Message{
		"Type":  "call",
		"Func":  "RegisterMicMuteStateListener",
		"Param": reg,
	}
	_, ok := write(msg, false)
	return ok
}

type MenuData struct {
	ID     uint
	Handle uintptr
}

func RegisterInitMenuPopupListener(menus []uintptr) bool {
	_, ok := write(Message{
		"Type":   "call",
		"Func":   "RegisterInitMenuPopupListener",
		"Params": menus,
	}, false)
	return ok
}

func CreateRootMenuItem(item Message) (bool, []MenuData) {
	ret, ok := write(Message{
		"Type":   "call",
		"Func":   "CreateRootMenuItem",
		"Params": item,
	}, true)
	if !ok {
		return false, nil
	}
	var data []MenuData
	for _, v := range ret["Value"].([]any) {
		item := v.(Message)
		data = append(data, MenuData{
			ID:     uint(item["ID"].(float64)),
			Handle: uintptr(item["Handle"].(float64)),
		})
	}
	return true, data
}

func write(msg Message, wait bool) (ret Message, ok bool) {
	var retChan chan Message
	if wait {
		retChan = make(chan Message)
	}
	item := writeQueueItem{Msg: msg, Ret: retChan}
	select {
	case <-done:
		return nil, false
	case writeQueue <- item:
		if retChan == nil {
			return nil, true
		}
		return <-retChan, true
	}
}

var done = make(chan struct{})

func Done() <-chan struct{} {
	return done
}

type writeQueueItem struct {
	Msg Message
	Ret chan<- Message
}

var writeQueue = make(chan writeQueueItem)

var pendingCall sync.Map

func writeLoop() {
	for {
		select {
		case <-done:
			return
		case item, ok := <-writeQueue:
			if !ok {
				return
			}
			err := writeMsg(item.Msg, item.Ret)
			if err != nil {
				log.Print(err)
				return
			}
		}
	}
}

func readLoop() {
	for {
		msg, err := readMsg()
		if err != nil {
			log.Print(err)
			return
		}
		processMsg(msg)
	}
}

func processMsg(msg Message) {
	switch msg["Type"] {
	case "return":
		if ret, ok := pendingCall.Load(uint(msg["Call"].(float64))); ok {
			ret.(chan<- Message) <- msg
			return
		}
	case "call":
		switch msg["Func"] {
		case "InitMenuPopupListener":
			if OnInitMenuPopup != nil {
				go func() {
					var ret = OnInitMenuPopup(uintptr(msg["Params"].(float64)))
					if _, ok := write(Message{
						"Type":  "return",
						"Func":  "InitMenuPopupListener",
						"Call":  uint(msg["ID"].(float64)),
						"Value": ret,
					}, false); !ok {
						log.Print("write after done")
					}
				}()
			}
			return
		case "Unload":
			if OnUnload != nil {
				OnUnload()
			}
			os.Exit(0)
			return
		}
	}
	if OnMessage != nil {
		OnMessage(msg)
	}
}

func readMsg() (Message, error) {
	var lenBuf [2]byte
	if _, err := io.ReadFull(os.Stdin, lenBuf[:]); err != nil {
		return nil, err
	}
	var len = binary.LittleEndian.Uint16(lenBuf[:])
	var msg Message
	if err := json.NewDecoder(io.LimitReader(os.Stdin, int64(len))).Decode(&msg); err != nil {
		return nil, err
	}
	return msg, nil
}

var lastMsgID uint = 1

func writeMsg(msg Message, ret chan<- Message) (err error) {
	defer func() {
		if ret != nil {
			pendingCall.Delete(lastMsgID)
		}
	}()

	if ret != nil {
		pendingCall.Store(lastMsgID, ret)
	}

	msg["ID"] = lastMsgID
	var buf bytes.Buffer
	buf.WriteByte(0)
	buf.WriteByte(0)
	if err := json.NewEncoder(&buf).Encode(msg); err != nil {
		return err
	}
	if buf.Len() > 0xFFFF {
		return errors.New("message too large")
	}
	content := buf.Bytes()
	binary.LittleEndian.PutUint16(content, uint16(len(content)-2))
	if _, err := os.Stdout.Write(content); err != nil {
		return err
	}
	lastMsgID++
	return nil
}

func WriteHello() {
	writeMsg(Message{
		"Type": "call",
		"Func": "Hello",
		"Params": Message{
			"SDKVersion": 1,
			"Name":       "WebUI",
			"Version":    Message{"Major": 1, "Minor": 0}},
	}, nil)
}
