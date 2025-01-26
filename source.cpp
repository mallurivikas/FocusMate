#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Base64.h>

// WiFi credentials - REPLACE THESE
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// API credentials - REPLACE THESE
const char* geminiApiKey = "YOUR_GEMINI_API_KEY";
const char* googleTTSApiKey = "YOUR_GOOGLE_TTS_API_KEY";
const char* googleSTTApiKey = "YOUR_GOOGLE_SPEECH_TO_TEXT_API_KEY";
const char* host = "generativelanguage.googleapis.com";

// Server configuration
WebServer server(80);
String generatedResponse = "No response yet";

// Text-to-Speech Function
String textToSpeech(String text) {
  WiFiClientSecure client;
  client.setInsecure();

  const char* ttsHost = "texttospeech.googleapis.com";
  String url = "/v1/text:synthesize?key=" + String(googleTTSApiKey);

  StaticJsonDocument<512> doc;
  JsonObject input = doc.createNestedObject("input");
  input["text"] = text;
  
  JsonObject voice = doc.createNestedObject("voice");
  voice["languageCode"] = "en-US";
  voice["name"] = "en-US-Neural2-A";
  
  JsonObject audioConfig = doc.createNestedObject("audioConfig");
  audioConfig["audioEncoding"] = "MP3";

  String requestBody;
  serializeJson(doc, requestBody);

  if (!client.connect(ttsHost, 443)) {
    return "TTS Connection failed!";
  }

  client.println("POST " + url + " HTTP/1.1");
  client.println("Host: " + String(ttsHost));
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(requestBody.length()));
  client.println("Connection: close");
  client.println();
  client.println(requestBody);

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 10000) {
      return "TTS Request Timeout!";
    }
  }

  String response = "";
  bool jsonStarted = false;
  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line.startsWith("{")) {
      jsonStarted = true;
    }
    if (jsonStarted) {
      response += line;
    }
  }

  StaticJsonDocument<2048> responseDoc;
  DeserializationError error = deserializeJson(responseDoc, response);
  
  if (error) {
    return "JSON parsing error: " + String(error.c_str());
  }

  if (responseDoc.containsKey("audioContent")) {
    return responseDoc["audioContent"].as<String>();
  }

  return "Unable to extract audio";
}

// Speech-to-Text Function
String speechToText(String base64Audio) {
  WiFiClientSecure client;
  client.setInsecure();

  const char* sttHost = "speech.googleapis.com";
  String url = "/v1/speech:recognize?key=" + String(googleSTTApiKey);

  StaticJsonDocument<1024> doc;
  doc["config"]["encoding"] = "WEBM_OPUS";
  doc["config"]["sampleRateHertz"] = 48000;
  doc["config"]["languageCode"] = "en-US";
  doc["audio"]["content"] = base64Audio;

  String requestBody;
  serializeJson(doc, requestBody);

  if (!client.connect(sttHost, 443)) {
    return "STT Connection failed!";
  }

  client.println("POST " + url + " HTTP/1.1");
  client.println("Host: " + String(sttHost));
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(requestBody.length()));
  client.println("Connection: close");
  client.println();
  client.println(requestBody);

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 10000) {
      return "STT Request Timeout!";
    }
  }

  String response = "";
  while (client.available()) {
    response += client.readString();
  }

  StaticJsonDocument<1024> responseDoc;
  DeserializationError error = deserializeJson(responseDoc, response);
  
  if (error) {
    return "JSON parsing error: " + String(error.c_str());
  }

  if (responseDoc["results"][0]["alternatives"][0].containsKey("transcript")) {
    return responseDoc["results"][0]["alternatives"][0]["transcript"].as<String>();
  }

  return "Unable to transcribe audio";
}

// HTML Content 
const char* html = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>FocusMate</title>
    <style>
        body {
            font-family: 'Arial', sans-serif;
            background-color: #121221;
            color: #e0e0ff;
            max-width: 600px;
            margin: 0 auto;
            padding: 20px;
            line-height: 1.6;
        }
        .container {
            background-color: #1e1e33;
            border-radius: 12px;
            box-shadow: 0 8px 15px rgba(0,0,0,0.4);
            padding: 30px;
        }
        #prompt {
            width: 100%;
            padding: 12px;
            margin-bottom: 15px;
            background-color: #2c2c44;
            color: #e0e0ff;
            border: 1px solid #6a5acd;
            border-radius: 6px;
        }
        #generate-btn, #voice-btn {
            width: 100%;
            padding: 12px;
            background-color: #6a5acd;
            color: white;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            margin-bottom: 15px;
        }
        #voice-btn.recording {
            background-color: #ff4500;
        }
        #response, #audio-container {
            margin-top: 20px;
            padding: 15px;
            background-color: #2c2c44;
            border-radius: 6px;
            border: 1px solid #6a5acd;
            min-height: 100px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>FocusMate</h1>
        <form id="prompt-form">
            <input type="text" id="prompt" name="prompt" placeholder="Enter your prompt here..." required>
            <button type="submit" id="generate-btn">Generate Response</button>
            <button type="button" id="voice-btn">🎤 Voice Input</button>
        </form>
        <div id="response"></div>
        <div id="audio-container"></div>
    </div>

    <script>
        const googleSTTApiKey = 'YOUR_GOOGLE_SPEECH_TO_TEXT_API_KEY';
        let mediaRecorder;
        let audioChunks = [];

        document.getElementById('voice-btn').addEventListener('click', startVoiceInput);
        document.getElementById('prompt-form').addEventListener('submit', handleSubmit);

        function startVoiceInput() {
            const voiceBtn = document.getElementById('voice-btn');
            
            if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
                alert('Voice input not supported in this browser.');
                return;
            }

            if (voiceBtn.classList.contains('recording')) {
                stopRecording();
            } else {
                navigator.mediaDevices.getUserMedia({ audio: true })
                    .then(stream => {
                        mediaRecorder = new MediaRecorder(stream);
                        audioChunks = [];

                        mediaRecorder.ondataavailable = event => {
                            audioChunks.push(event.data);
                        };

                        mediaRecorder.onstop = sendAudioToServerSTT;

                        mediaRecorder.start();
                        voiceBtn.classList.add('recording');
                        voiceBtn.textContent = '🛑 Stop Recording';
                    })
                    .catch(error => {
                        console.error('Error accessing microphone:', error);
                        alert('Could not access microphone. Please check permissions.');
                    });
            }
        }

        function stopRecording() {
            const voiceBtn = document.getElementById('voice-btn');
            if (mediaRecorder && mediaRecorder.state !== 'inactive') {
                mediaRecorder.stop();
                voiceBtn.classList.remove('recording');
                voiceBtn.textContent = '🎤 Voice Input';
            }
        }

        function sendAudioToServerSTT() {
            const audioBlob = new Blob(audioChunks, { type: 'audio/webm' });
            const reader = new FileReader();

            reader.onloadend = () => {
                const base64data = reader.result.split(',')[1];
                
                fetch('/transcribe', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/x-www-form-urlencoded',
                    },
                    body: 'audio=' + encodeURIComponent(base64data)
                })
                .then(response => response.text())
                .then(transcription => {
                    document.getElementById('prompt').value = transcription;
                })
                .catch(error => {
                    console.error('Error:', error);
                    alert('Transcription failed');
                });
            };

            reader.readAsDataURL(audioBlob);
        }

        function handleSubmit(e) {
            e.preventDefault();
            const prompt = document.getElementById('prompt').value;
            const responseDiv = document.getElementById('response');
            const audioDiv = document.getElementById('audio-container');
            
            responseDiv.innerHTML = 'Generating response...';
            audioDiv.innerHTML = '';

            fetch('/generate', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: 'prompt=' + encodeURIComponent(prompt)
            })
            .then(response => response.text())
            .then(data => {
                const [text, audioBase64] = data.split('||');
                responseDiv.innerHTML = text;
                
                if (audioBase64 && audioBase64 !== 'TTS Request Timeout!') {
                    const audioElement = document.createElement('audio');
                    audioElement.src = 'data:audio/mp3;base64,' + audioBase64;
                    audioElement.controls = true;
                    audioDiv.appendChild(audioElement);
                }
            })
            .catch(error => {
                responseDiv.innerHTML = 'Error: ' + error.message;
            });
        }
    </script>
</body>
</html>
)";

void setup() {
  Serial.begin(115200);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  server.on("/", handleRoot);
  server.on("/generate", handleGenerate);
  server.on("/transcribe", handleTranscribe);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}

void handleRoot() {
  server.send(200, "text/html", html);
}

void handleTranscribe() {
  if (server.method() == HTTP_POST) {
    String base64Audio = server.arg("audio");
    String transcription = speechToText(base64Audio);
    server.send(200, "text/plain", transcription);
  }
}

void handleGenerate() {
  if (server.method() == HTTP_POST) {
    String prompt = server.arg("prompt");
    generatedResponse = sendRequest(prompt);
    String audioData = textToSpeech(generatedResponse);
    
    String fullResponse = generatedResponse + "||" + audioData;
    server.send(200, "text/plain", fullResponse);
  }
}

String sendRequest(String prompt) {
  WiFiClientSecure client;
  client.setInsecure();
  
  if (!client.connect(host, 443)) {
    return "Connection to server failed!";
  }
  
  StaticJsonDocument<2048> doc;
  JsonObject contents = doc.createNestedObject("contents");
  JsonArray parts = contents.createNestedArray("parts");
  JsonObject part = parts.createNestedObject();
  part["text"] = prompt;
  String requestBody;
  serializeJson(doc, requestBody);
  
  String url = "https://generativelanguage.googleapis.com/v1/models/gemini-1.5-flash:generateContent?key=" + String(geminiApiKey);
  
  client.println("POST " + url + " HTTP/1.1");
  client.println("Host: " + String(host));
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(requestBody.length()));
  client.println("Connection: close");
  client.println();
  client.println(requestBody);
  
  unsigned long startTime = millis();
  while (!client.available()) {
    if (millis() - startTime > 10000) {
      return "Response Timeout!";
    }
    delay(100);
  }
  
  String response = "";
  while (client.available()) {
    response += client.readString();
  }
  
  int jsonStart = response.indexOf("{");
  if (jsonStart != -1) {
    response = response.substring(jsonStart);
  }
  
  StaticJsonDocument<4096> responseDoc;
  DeserializationError error = deserializeJson(responseDoc, response);
  
  if (error) {
    return "JSON parsing error: " + String(error.c_str());
  }
  
  if (responseDoc["candidates"][0]["content"]["parts"][0].containsKey("text")) {
    return responseDoc["candidates"][0]["content"]["parts"][0]["text"].as<String>();
  }
  
  return "Unable to extract text";
}
