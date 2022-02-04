import json
import pprint
import websocket
from websocket import create_connection

# Create a dictionary to be sent.
json_data = {"conf": {"state": "0"}}

websocket.enableTrace(True)
ws = create_connection('ws://10.0.10.96/ws')

ws.send(json.dumps(json_data))
result = ws.recv()
print('Result: {}'.format(result))


# {
#   "conf": {
#     "laps": 40,
#     "players": 4,
#     "gates": 3,
#     "light": 0,
#     "light_brightness": 255,
#     "state": 0,
#     "names": [
#       {
#         "id": 0,
#         "name": "Player 1",
#         "color": "blue"
#       },
#       {
#         "id": 0,
#         "name": "Player 2",
#         "color": "red"
#       },
#       {
#         "id": 0,
#         "name": "Player 3",
#         "color": "green"
#       },
#       {
#         "id": 0,
#         "name": "Player 4",
#         "color": "yellow"
#       }
#     ]
#   }
# }

# {
#   "race": {
#     "state": "WAIT",
#     "lap": 0,
#     "time": 0,
#     "message": "test Char pointer"
#   }
# }

