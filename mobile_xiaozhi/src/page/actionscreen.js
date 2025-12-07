import React, { useState } from "react";
import { View, Text, Button, StyleSheet, TextInput } from "react-native";

export default function ActionScreen({ route, navigation }) {
  const { ws } = route.params;

  const [steps, setSteps] = useState("3");
  const [speed, setSpeed] = useState("700");
  const [direction, setDirection] = useState("1");

  const sendWalk = () => {
    if (!ws || ws.readyState !== 1) {
      console.log("❌ WS chưa kết nối");
      return;
    }

    const json = {
      jsonrpc: "2.0",
      id: 1,
      method: "tools/call",
      params: {
        name: "self.zeri.action",
        arguments: {
          action: "walk",
          steps: parseInt(steps),
          speed: parseInt(speed),
          direction: parseInt(direction),
        }
      }
    };

    ws.send(JSON.stringify(json));
    console.log("📤 Sent WALK JSON with params:", json);
  };

  return (
    <View style={styles.container}>
      <Text style={styles.title}>Điều chỉnh tham số</Text>

      {/* SỐ BƯỚC */}
      <Text style={styles.label}>Số bước (steps):</Text>
      <TextInput
        style={styles.input}
        keyboardType="numeric"
        value={steps}
        onChangeText={setSteps}
      />

      {/* TỐC ĐỘ */}
      <Text style={styles.label}>Tốc độ (speed):</Text>
      <TextInput
        style={styles.input}
        keyboardType="numeric"
        value={speed}
        onChangeText={setSpeed}
      />

      {/* HƯỚNG ĐI */}
      <Text style={styles.label}>Direction (1 = tiến, -1 = lùi):</Text>
      <TextInput
        style={styles.input}
        keyboardType="numeric"
        value={direction}
        onChangeText={setDirection}
      />

      <View style={{ height: 20 }} />

      {/* BUTTON GỬI */}
      <Button title="🚀 Gửi lệnh Đi Bộ" onPress={sendWalk} />

      <View style={{ height: 40 }} />

      <Button title="← Quay lại Trang Chủ" onPress={() => navigation.goBack()} />
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, padding: 20, justifyContent: "flex-start" },
  title: { fontSize: 26, textAlign: "center", marginBottom: 20, fontWeight: "bold" },
  label: { fontSize: 16, marginBottom: 5 },
  input: {
    borderWidth: 1,
    borderColor: "#444",
    padding: 10,
    borderRadius: 5,
    marginBottom: 15,
  },
});
