export default {
  async fetch(request) {
    const url = new URL(request.url);
    const lat = url.searchParams.get("lat");
    const lon = url.searchParams.get("lon");

    if (!lat || !lon) {
      return new Response(JSON.stringify({ error: "lat and lon required" }), {
        status: 400,
        headers: { "Content-Type": "application/json" },
      });
    }

    const nwsUrl = `https://api.weather.gov/alerts/active?point=${lat},${lon}&status=actual&urgency=Immediate,Expected`;

    const resp = await fetch(nwsUrl, {
      headers: {
        "User-Agent": "ESP32-Sign-Proxy/1.0 (matt@schwartz.dev)",
        Accept: "application/geo+json",
      },
    });

    if (!resp.ok) {
      return new Response(
        JSON.stringify({ error: `NWS returned ${resp.status}` }),
        { status: 502, headers: { "Content-Type": "application/json" } }
      );
    }

    const data = await resp.json();

    // Extract just the event names to minimize payload for ESP32
    const alerts = (data.features || [])
      .map((f) => f.properties?.event)
      .filter(Boolean)
      .slice(0, 5);

    return new Response(JSON.stringify({ alerts, count: alerts.length }), {
      headers: {
        "Content-Type": "application/json",
        "Access-Control-Allow-Origin": "*",
        "Cache-Control": "max-age=120",
      },
    });
  },
};
