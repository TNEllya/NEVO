import { BrowserRouter as Router, Routes, Route, Navigate } from "react-router-dom";
import MainPage from "@/pages/MainPage";
import VideoCallPage from "@/pages/VideoCallPage";
import SettingsPage from "@/pages/SettingsPage";

export default function App() {
  return (
    <Router>
      <Routes>
        <Route path="/" element={<MainPage />} />
        <Route path="/video-call/:userId" element={<VideoCallPage />} />
        <Route path="/settings" element={<Navigate to="/settings/audio" replace />} />
        <Route path="/settings/:section" element={<SettingsPage />} />
        <Route path="*" element={<Navigate to="/" replace />} />
      </Routes>
    </Router>
  );
}
