import React, { useState, useRef } from "react";
import { View, Text, TextInput, Button, ScrollView } from "react-native";

export default function ConnectScreen({ navigation }) {
  const [ip, setIp] = useState("");
  const [log, setLog] = useState([]);
  const wsRef = useRef(null);

  const addLog = (msg) => setLog((prev) => [...prev, msg]);

  const connectWS = () => {
    if (!ip) {
      addLog("❌ Chưa nhập IP");
      return;
    }

    const url = `ws://${ip}:8080/ws`;
    addLog(`🔌 Kết nối: ${url}`);

    wsRef.current = new WebSocket(url);

    wsRef.current.onopen = () => {
      addLog("🟢 WS Connected!");

      // Chuyển sang trang điều khiển và truyền WebSocket
      navigation.navigate("Control", { ws: wsRef.current });
    };

    wsRef.current.onclose = () => addLog("🔴 WS Closed");
    wsRef.current.onerror = (e) => addLog("❌ WS Error: " + e.message);
    wsRef.current.onmessage = (msg) => addLog("📨 Recv: " + msg.data);
  };

  return (
    <View style={{ padding: 20, flex: 1 }}>
      <TextInput
        style={{
          borderWidth: 1,
          borderColor: "#444",
          padding: 10,
          marginBottom: 10,
          borderRadius: 5,
        }}
        placeholder="Nhập IP robot (VD: 192.168.1.17)"
        value={ip}
        onChangeText={setIp}
      />

      <Button title="KẾT NỐI WEBSOCKET" onPress={connectWS} />

      <ScrollView style={{ marginTop: 20, backgroundColor: "#eee", padding: 10, borderRadius: 5 }}>
        {log.map((l, i) => (
          <Text key={i} style={{ marginBottom: 5 }}>
            {l}
          </Text>
        ))}
      </ScrollView>
    </View>
  );
}
