import websocket

def on_message(wsapp, message):
  print(message)

websocket.setdefaulttimeout(300)
wsapp = websocket.WebSocketApp("ws://10.0.10.96/ws", on_message=on_message)
wsapp.run_forever()
# Program should print a "timed out" error message
