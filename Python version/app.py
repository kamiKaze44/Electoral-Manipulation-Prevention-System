from flask import Flask, render_template, request, redirect, url_for, flash

app = Flask(__name__)
app.secret_key = "elecguard-dev-key"

sample_reports = [
    {
        "id": 1,
        "manipulation_type": "Financial",
        "description": "A voter was allegedly offered money near a polling station in exchange for supporting a candidate.",
        "location": "Chisinau",
        "status": "Pending",
        "submitted_at": "2026-05-03 18:20",
        "incident_date": "2026-05-03",
        "reporter_name": "Anonymous",
        "ai_analysis": None
    },
    {
        "id": 2,
        "manipulation_type": "Administrative",
        "description": "Several public employees reported pressure from local officials to attend a political event and support a party.",
        "location": "Orhei",
        "status": "Under Review",
        "submitted_at": "2026-05-02 14:10",
        "incident_date": "2026-05-02",
        "reporter_name": "Citizen",
        "ai_analysis": "Potential abuse of administrative resources. Relevant legal references may include Art. 46 of the Electoral Code and anti-corruption provisions."
    },
    {
        "id": 3,
        "manipulation_type": "Informational",
        "description": "A coordinated social media campaign spread false information about polling station closures and voting dates.",
        "location": "Balti",
        "status": "Resolved",
        "submitted_at": "2026-05-01 09:30",
        "incident_date": "2026-04-30",
        "reporter_name": "Observer",
        "ai_analysis": "This may indicate an informational manipulation pattern affecting voter awareness and equal access to electoral information."
    }
]


@app.route("/")
def home():
    total_reports = len(sample_reports)
    recent_reports = sorted(sample_reports, key=lambda r: r["id"], reverse=True)[:3]
    return render_template(
        "index.html",
        total_reports=total_reports,
        recent_reports=recent_reports
    )


@app.route("/legislation")
def legislation():
    return render_template("legislation.html")


@app.route("/report", methods=["GET", "POST"])
def report():
    if request.method == "POST":
        flash("Prototype mode: report submission is not stored yet.", "success")
        return redirect(url_for("report"))
    return render_template("report.html")


@app.route("/reports")
def reports():
    active_filter = request.args.get("filter_type")
    types = [{"manipulation_type": t} for t in sorted(set(r["manipulation_type"] for r in sample_reports))]

    filtered_reports = sample_reports
    if active_filter:
        filtered_reports = [r for r in sample_reports if r["manipulation_type"] == active_filter]

    return render_template(
        "reports.html",
        reports=filtered_reports,
        types=types,
        active_filter=active_filter
    )


@app.route("/reports/<int:report_id>")
def report_detail(report_id):
    report = next((r for r in sample_reports if r["id"] == report_id), None)
    if not report:
        return "Report not found", 404
    return render_template("report_detail.html", report=report)


@app.route("/analyze/<int:report_id>", methods=["POST"])
def analyze(report_id):
    report = next((r for r in sample_reports if r["id"] == report_id), None)
    if not report:
        flash("Report not found.", "error")
        return redirect(url_for("reports"))

    if not report["ai_analysis"]:
        report["ai_analysis"] = (
            "Prototype analysis: this report has been reviewed automatically based on "
            "its manipulation category, description, and possible legal relevance. "
            "A future version may generate a more detailed legal and risk assessment."
        )

    flash(f"Prototype analysis completed for report #{report_id}.", "success")
    return redirect(url_for("report_detail", report_id=report_id))


@app.route("/close-report/<int:report_id>", methods=["POST"])
def close_report(report_id):
    report = next((r for r in sample_reports if r["id"] == report_id), None)
    if not report:
        flash("Report not found.", "error")
        return redirect(url_for("reports"))

    report["status"] = "Resolved"
    flash(f"Report #{report_id} marked as resolved.", "success")
    return redirect(url_for("report_detail", report_id=report_id))


if __name__ == "__main__":
    app.run(debug=True)