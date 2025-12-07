import React from "react";
import { View, Text, Button, StyleSheet } from "react-native";

export default function HomePage({ navigation, route }) {
  const { ws } = route.params;  // ⭐ NHẬN WS từ connectpage

  return (
    <View style={styles.container}>
      <Text style={styles.title}>Home Page</Text>

      <Button title="Nút 1" onPress={() => {}} />
      <View style={styles.spacer} />

      <Button title="Nút 2" onPress={() => {}} />
      <View style={styles.spacer} />

      <Button title="Nút 3" onPress={() => {}} />
      <View style={styles.spacer} />

      {/* ⭐ TRUYỀN TIẾP WS sang ActionScreen */}
      <Button
        title="Action"
        onPress={() => navigation.navigate("ActionScreen", { ws })}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, justifyContent: "center", padding: 20 },
  title: { fontSize: 26, textAlign: "center", marginBottom: 40, fontWeight: "bold" },
  spacer: { height: 20 },
});
