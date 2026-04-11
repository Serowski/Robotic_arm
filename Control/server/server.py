import socket
import json
import time
import threading

def receive_from_esp(client):
    """Czyta odpowiedzi z ESP w tle"""
    while True:
        try:
            data = client.recv(1024)
            if not data:
                print("ESP32 się rozłączył")
                break
            print(f"\n   < ESP32 odpisał: {data.decode().strip()}")
            #print("> Wpisz komendę: ", end="", flush=True)
        except:
            break

def run_server():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('0.0.0.0', 5000))
    server.listen(1)
    
    print("\n" + "="*60)
    print(" SERWER CZEKA NA ESP32 NA PORCIE 5000")
    print("="*60)
    print("IP serwera: 0.0.0.0:5000\n")
    
    client, addr = server.accept()
    print(f"  ESP32 PODŁĄCZONY!")
    print(f"   Adres IP ESP: {addr[0]}")
    print(f"   Port: {addr[1]}\n")
    
    # Uruchom wątek do czytania
    recv_thread = threading.Thread(target=receive_from_esp, args=(client,), daemon=True)
    recv_thread.start()
    
    # Menu
    print("DOSTĘPNE KOMENDY:")
    print("-"*60)
    print("  sv [channel] [angle]")
    print("  st [angle]")
    print("  ping         → Test połączenia")
    print("  quit         → Zamknij")
    print("-"*60 + "\n")
    
    try:
        while True:
            cmd_input = input("> Wpisz komendę: ").strip()
            
            if cmd_input.lower() == "quit":
                print("Zamykanie...")
                break
            
            # Ping
            if cmd_input.lower() == "ping":
                msg = {"type": "ping"}
            
            # Servo: sv <kanał> <kąt>
            elif cmd_input.lower().startswith("sv"):
                try:
                    parts = cmd_input.split()
                    channel = int(parts[1])
                    angle = float(parts[2])
                    if not (0 <= channel <= 15):
                        print("Kanał powinien być 0-15")
                        continue
                    if not (0 <= angle <= 180):
                        print("Kąt powinien być 0-180°")
                        continue
                    msg = {"type": "servo", "channel": channel, "angle": angle}
                except (IndexError, ValueError):
                    print("Zły format")
                    continue
            
            # Stepper: st <kąt>
            elif cmd_input.lower().startswith("st"):
                try:
                    parts = cmd_input.split()
                    angle = float(parts[1])
                    if not (-180 <= angle <= 180):
                        print(" Kąt powinien być -180 do 180°")
                        continue
                    msg = {"type": "stepper", "angle": angle}
                except (IndexError, ValueError):
                    print("Zły format")
                    continue
            
            else:
                print(" Nieznana komenda")
                continue
            
            # Wysyłanie
            json_msg = json.dumps(msg) + "\n"
            try:
                client.send(json_msg.encode())
                print(f"   < Wysłano: {json_msg.strip()}")
            except:
                print(" Błąd wysyłania - utracono połączenie")
                break
            
            time.sleep(0.2)
    
    except KeyboardInterrupt:
        print("\n\nPrzerwano...")
    finally:
        client.close()
        server.close()

if __name__ == "__main__":
    run_server()