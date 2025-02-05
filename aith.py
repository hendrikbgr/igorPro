
# === User Configuration ===
CLIENT_ID = "892d24efb7be46eda64b117e84746469"       # Replace with your Spotify Client ID
CLIENT_SECRET = "efc676374890412bb6dc7ae163f41539"   # Replace with your Spotify Client Secret
from flask import Flask, request
import requests
import webbrowser

app = Flask(__name__)

REDIRECT_URI = "http://localhost:8888/callback"
SCOPE = "user-read-playback-state user-modify-playback-state"

@app.route("/callback")
def callback():
    error = request.args.get("error")
    if error:
        return f"Error: {error}"
    code = request.args.get("code")
    if not code:
        return "No code provided."
    
    # Exchange the code for an access token and refresh token.
    token_url = "https://accounts.spotify.com/api/token"
    data = {
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": REDIRECT_URI,
        "client_id": CLIENT_ID,
        "client_secret": CLIENT_SECRET
    }
    headers = {"Content-Type": "application/x-www-form-urlencoded"}
    response = requests.post(token_url, data=data, headers=headers)
    if response.status_code != 200:
        return f"Token exchange failed: {response.text}"
    
    token_info = response.json()
    access_token = token_info.get("access_token")
    refresh_token = token_info.get("refresh_token")
    expires_in = token_info.get("expires_in")
    
    print("Access Token:", access_token)
    print("Refresh Token:", refresh_token)
    print("Expires in:", expires_in, "seconds")
    
    return "<h2>Tokens obtained. Check your console.</h2>"

def main():
    auth_url = "https://accounts.spotify.com/authorize"
    params = {
        "client_id": CLIENT_ID,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "scope": SCOPE,
        "show_dialog": "true"
    }
    req = requests.Request("GET", auth_url, params=params).prepare()
    url = req.url
    print("Opening the following URL in your browser:")
    print(url)
    webbrowser.open(url)
    app.run(port=8888)

if __name__ == "__main__":
    main()
